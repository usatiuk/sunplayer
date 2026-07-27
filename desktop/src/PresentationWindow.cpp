#include "PresentationWindow.h"

#include <array>
#include <cmath>
#include <cstring>
#include <memory>

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QResizeEvent>
#include <QWheelEvent>

#include <QtCore/qt_windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "PresentationOutputState.h"

using Microsoft::WRL::ComPtr;

namespace {
constexpr DXGI_FORMAT swapChainFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;

QString hresultMessage(HRESULT result) {
    return QStringLiteral("0x%1").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

ComPtr<ID3DBlob> compileShader(const char *source, const char *entry, const char *profile) {
    ComPtr<ID3DBlob> bytecode;
    ComPtr<ID3DBlob> errors;
    const HRESULT result = D3DCompile(
        source, std::strlen(source), nullptr, nullptr, nullptr, entry, profile,
        D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
        &bytecode, &errors);
    if (FAILED(result)) {
        const QByteArray message = errors
            ? QByteArray(static_cast<const char *>(errors->GetBufferPointer()),
                         static_cast<qsizetype>(errors->GetBufferSize()))
            : QByteArray();
        qFatal("Shader compilation failed (%s): %s",
               qPrintable(hresultMessage(result)), message.constData());
    }
    return bytecode;
}

const char vertexShaderSource[] = R"(
struct Output {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

Output main(uint vertexId : SV_VertexID) {
    Output output;
    const float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}
)";

const char pixelShaderSource[] = R"(
Texture2D<float4> uiTexture : register(t0);
SamplerState uiSampler : register(s0);

cbuffer Params : register(b0) {
    float2 viewportSize;
    float2 canvasOrigin;
    float2 canvasSize;
    float sourcePeak;
    float targetPeak;
    float phase;
    float toneMappingEnabled;
    float hdrActive;
    float sdrScale;
};

static const float tau = 6.28318530718;

float3 srgbToLinear(float3 value) {
    const float3 low = 1.0 - step(0.04045, value);
    const float3 linearPart = value / 12.92;
    const float3 powerPart = pow((value + 0.055) / 1.055, 2.4);
    return lerp(powerPart, linearPart, low);
}

float3 spectrum(float position) {
    return 0.5 + 0.5 * cos(tau * (position + float3(0.0, 0.33, 0.67)));
}

float toneMapSignal(float signal) {
    const float source = max(sourcePeak, 1.0);
    const float target = max(targetPeak, 1.0);
    if (toneMappingEnabled < 0.5)
        return signal;
    const float normalized = signal / target;
    const float whitePoint = max(source / target, 1.0);
    const float mapped = normalized
        * (1.0 + normalized / (whitePoint * whitePoint))
        / (1.0 + normalized);
    return target * mapped;
}

float3 toneMap(float3 color) {
    const float signal = max(color.r, max(color.g, color.b));
    return signal > 0.0 ? color * (toneMapSignal(signal) / signal) : color;
}

float3 pattern(float2 uv) {
    const float ramp = lerp(0.02, sourcePeak, smoothstep(0.0, 1.0, uv.x));
    float3 color;
    if (uv.y < 0.33)
        color = float3(ramp, ramp, ramp);
    else if (uv.y < 0.66)
        color = spectrum(uv.x + phase) * ramp;
    else {
        const float stepValue = floor(uv.x * 8.0) / 7.0;
        const float level = lerp(0.0, sourcePeak, stepValue);
        color = float3(level, level, level);
    }
    const float separator =
        step(0.008, abs(uv.y - 0.33)) * step(0.008, abs(uv.y - 0.66));
    return toneMap(color) * separator * (hdrActive > 0.5 ? sdrScale : 1.0);
}

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    const float3 background = srgbToLinear(float3(17.0, 19.0, 24.0) / 255.0)
        * (hdrActive > 0.5 ? sdrScale : 1.0);
    float3 color = background;

    const float2 pixel = uv * viewportSize;
    if (all(pixel >= canvasOrigin) && all(pixel < canvasOrigin + canvasSize)) {
        const float2 canvasUv = (pixel - canvasOrigin) / canvasSize;
        color = pattern(canvasUv);
    }

    const float4 ui = uiTexture.SampleLevel(uiSampler, uv, 0.0);
    const float alpha = saturate(ui.a);
    const float3 encodedStraight = alpha > 0.00001
        ? saturate(ui.rgb / alpha)
        : 0.0;
    const float3 uiLinear = srgbToLinear(encodedStraight)
        * (hdrActive > 0.5 ? sdrScale : 1.0);
    color = uiLinear * alpha + color * (1.0 - alpha);
    return float4(color, 1.0);
}
)";

class RenderControl final : public QQuickRenderControl {
public:
    explicit RenderControl(QWindow *window) : m_window(window) {}

    QWindow *renderWindow(QPoint *offset) override {
        if (offset)
            *offset = {};
        return m_window;
    }

private:
    QWindow *m_window;
};

struct alignas(16) ShaderParams {
    std::array<float, 2> viewportSize{};
    std::array<float, 2> canvasOrigin{};
    std::array<float, 2> canvasSize{};
    float sourcePeak = 12.5f;
    float targetPeak = 1.0f;
    float phase = 0.0f;
    float toneMappingEnabled = 1.0f;
    float hdrActive = 0.0f;
    float sdrScale = 1.0f;
};
static_assert(sizeof(ShaderParams) % 16 == 0);
}

class PresentationWindow::Impl {
public:
    explicit Impl(PresentationWindow *window)
        : q(window) {
        initializeDevice();
        outputState = std::make_unique<PresentationOutputState>(q);
        initializeQuick();
    }

    ~Impl() {
        if (renderControl)
            renderControl->invalidate();
        rootItem.reset();
        qmlComponent.reset();
        qmlEngine.reset();
        quickWindow.reset();
        renderControl.reset();
        releaseUiTarget();
        releaseSwapChain();
    }

    void initializeDevice() {
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        const std::array featureLevels{
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };
        D3D_FEATURE_LEVEL selectedLevel{};
        HRESULT result = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            featureLevels.data(), static_cast<UINT>(featureLevels.size()),
            D3D11_SDK_VERSION, &device, &selectedLevel, &context);
        if (result == E_INVALIDARG) {
            result = D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                featureLevels.data() + 1, 1, D3D11_SDK_VERSION,
                &device, &selectedLevel, &context);
        }
        if (FAILED(result))
            qFatal("Could not create D3D11 device: %s", qPrintable(hresultMessage(result)));

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        result = device.As(&dxgiDevice);
        if (SUCCEEDED(result))
            result = dxgiDevice->GetAdapter(&adapter);
        if (SUCCEEDED(result))
            result = adapter->GetParent(IID_PPV_ARGS(&factory));
        if (FAILED(result))
            qFatal("Could not obtain DXGI factory: %s", qPrintable(hresultMessage(result)));

        const auto vertexBytecode = compileShader(vertexShaderSource, "main", "vs_5_0");
        const auto pixelBytecode = compileShader(pixelShaderSource, "main", "ps_5_0");
        result = device->CreateVertexShader(
            vertexBytecode->GetBufferPointer(), vertexBytecode->GetBufferSize(),
            nullptr, &vertexShader);
        if (SUCCEEDED(result)) {
            result = device->CreatePixelShader(
                pixelBytecode->GetBufferPointer(), pixelBytecode->GetBufferSize(),
                nullptr, &pixelShader);
        }
        if (FAILED(result))
            qFatal("Could not create compositor shaders: %s", qPrintable(hresultMessage(result)));

        D3D11_BUFFER_DESC bufferDescription{};
        bufferDescription.ByteWidth = sizeof(ShaderParams);
        bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
        bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        result = device->CreateBuffer(&bufferDescription, nullptr, &constantBuffer);
        if (FAILED(result))
            qFatal("Could not create compositor constants: %s", qPrintable(hresultMessage(result)));

        D3D11_SAMPLER_DESC samplerDescription{};
        samplerDescription.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        samplerDescription.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        samplerDescription.MaxLOD = D3D11_FLOAT32_MAX;
        result = device->CreateSamplerState(&samplerDescription, &sampler);
        if (FAILED(result))
            qFatal("Could not create compositor sampler: %s", qPrintable(hresultMessage(result)));
    }

    void initializeQuick() {
        renderControl = std::make_unique<RenderControl>(q);
        QObject::connect(renderControl.get(), &QQuickRenderControl::renderRequested,
                         q, [this] { markQuickDirty(); });
        QObject::connect(renderControl.get(), &QQuickRenderControl::sceneChanged,
                         q, [this] { markQuickDirty(); });

        quickWindow = std::make_unique<QQuickWindow>(renderControl.get());
        quickWindow->setColor(Qt::transparent);
        quickWindow->setGraphicsDevice(
            QQuickGraphicsDevice::fromDeviceAndContext(device.Get(), context.Get()));

        qmlEngine = std::make_unique<QQmlEngine>();
        qmlEngine->rootContext()->setContextProperty(
            QStringLiteral("outputState"), outputState.get());
        qmlComponent = std::make_unique<QQmlComponent>(qmlEngine.get());
        qmlComponent->loadFromModule(QStringLiteral("Sunroom"), QStringLiteral("Main"));
        if (qmlComponent->isError()) {
            for (const auto &error : qmlComponent->errors())
                qWarning() << error;
            qFatal("Could not load the Sunroom QML scene");
        }

        QObject *object = qmlComponent->create();
        rootItem.reset(qobject_cast<QQuickItem *>(object));
        if (!rootItem)
            qFatal("Sunroom Main.qml must have an Item root");
        rootItem->setParentItem(quickWindow->contentItem());
        updateQuickGeometry();
        rootItem->forceActiveFocus();

        if (!renderControl->initialize())
            qFatal("Could not initialize redirected Qt Quick rendering");
    }

    QSize pixelSize() const {
        const QSizeF physical = QSizeF(q->size()) * q->devicePixelRatio();
        return {
            std::max(8, qRound(physical.width())),
            std::max(8, qRound(physical.height())),
        };
    }

    void updateQuickGeometry() {
        if (!quickWindow || !rootItem)
            return;
        quickWindow->setGeometry(0, 0, q->width(), q->height());
        quickWindow->contentItem()->setSize(q->size());
        rootItem->setSize(q->size());
    }

    void createSwapChain() {
        if (swapChain)
            return;
        DXGI_SWAP_CHAIN_DESC1 description{};
        const QSize size = pixelSize();
        description.Width = static_cast<UINT>(size.width());
        description.Height = static_cast<UINT>(size.height());
        description.Format = swapChainFormat;
        description.SampleDesc.Count = 1;
        description.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        description.BufferCount = 2;
        description.Scaling = DXGI_SCALING_STRETCH;
        description.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        description.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        HRESULT result = factory->CreateSwapChainForHwnd(
            device.Get(), reinterpret_cast<HWND>(q->winId()), &description,
            nullptr, nullptr, &swapChain);
        if (FAILED(result))
            qFatal("Could not create FP16 swapchain: %s", qPrintable(hresultMessage(result)));
        factory->MakeWindowAssociation(
            reinterpret_cast<HWND>(q->winId()), DXGI_MWA_NO_ALT_ENTER);

        ComPtr<IDXGISwapChain3> swapChain3;
        if (SUCCEEDED(swapChain.As(&swapChain3))) {
            UINT support = 0;
            result = swapChain3->CheckColorSpaceSupport(
                DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709, &support);
            if (SUCCEEDED(result)
                && (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
                swapChain3->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709);
            } else {
                qWarning("DXGI did not report scRGB presentation support");
            }
        }
        createBackBuffer();
    }

    void createBackBuffer() {
        backBuffer.Reset();
        backBufferView.Reset();
        HRESULT result = swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
        if (SUCCEEDED(result))
            result = device->CreateRenderTargetView(backBuffer.Get(), nullptr, &backBufferView);
        if (FAILED(result))
            qFatal("Could not create FP16 backbuffer view: %s", qPrintable(hresultMessage(result)));
        swapChainSize = pixelSize();
    }

    void resizeSwapChain() {
        const QSize size = pixelSize();
        if (!swapChain || size == swapChainSize)
            return;
        context->OMSetRenderTargets(0, nullptr, nullptr);
        backBufferView.Reset();
        backBuffer.Reset();
        const HRESULT result = swapChain->ResizeBuffers(
            2, static_cast<UINT>(size.width()), static_cast<UINT>(size.height()),
            swapChainFormat, 0);
        if (FAILED(result))
            qFatal("Could not resize FP16 swapchain: %s", qPrintable(hresultMessage(result)));
        createBackBuffer();
    }

    void releaseSwapChain() {
        backBufferView.Reset();
        backBuffer.Reset();
        swapChain.Reset();
        swapChainSize = {};
    }

    void createUiTarget() {
        const QSize size = pixelSize();
        if (uiTexture && uiTextureSize == size)
            return;
        releaseUiTarget();

        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(size.width());
        description.Height = static_cast<UINT>(size.height());
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        HRESULT result = device->CreateTexture2D(&description, nullptr, &uiTexture);
        if (SUCCEEDED(result))
            result = device->CreateShaderResourceView(uiTexture.Get(), nullptr, &uiTextureView);
        if (FAILED(result))
            qFatal("Could not create the Qt Quick FP16 target: %s",
                   qPrintable(hresultMessage(result)));

        QQuickRenderTarget target = QQuickRenderTarget::fromD3D11Texture(
            uiTexture.Get(), static_cast<uint>(description.Format), size, 1);
        target.setDevicePixelRatio(q->devicePixelRatio());
        quickWindow->setRenderTarget(target);
        uiTextureSize = size;
        quickDirty = true;
    }

    void releaseUiTarget() {
        uiTextureView.Reset();
        uiTexture.Reset();
        uiTextureSize = {};
    }

    void renderQuick() {
        createUiTarget();
        if (!quickDirty)
            return;
        quickDirty = false;
        renderControl->polishItems();
        renderControl->beginFrame();
        renderControl->sync();
        renderControl->render();
        renderControl->endFrame();
    }

    float rootFloat(const char *name, float fallback) const {
        const QVariant value = rootItem->property(name);
        return value.isValid() ? value.toFloat() : fallback;
    }

    void updateConstants() {
        ShaderParams params;
        params.viewportSize = {
            static_cast<float>(swapChainSize.width()),
            static_cast<float>(swapChainSize.height()),
        };
        const float dpr = static_cast<float>(q->devicePixelRatio());
        params.canvasOrigin = {
            rootFloat("canvasX", 24.0f) * dpr,
            rootFloat("canvasY", 112.0f) * dpr,
        };
        params.canvasSize = {
            rootFloat("canvasWidth", q->width() - 48.0f) * dpr,
            rootFloat("canvasHeight", q->height() - 220.0f) * dpr,
        };
        params.sourcePeak = rootFloat("sourcePeakHeadroom", 12.5f);
        params.targetPeak = rootFloat("effectiveTargetHeadroom", 1.0f);
        params.phase = rootFloat("phase", 0.0f);
        params.toneMappingEnabled = rootItem->property("toneMappingEnabled").toBool()
            ? 1.0f : 0.0f;
        params.hdrActive = outputState->hdrActive() ? 1.0f : 0.0f;
        params.sdrScale = outputState->sdrScale();

        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT result = context->Map(
            constantBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (SUCCEEDED(result)) {
            std::memcpy(mapped.pData, &params, sizeof(params));
            context->Unmap(constantBuffer.Get(), 0);
        }
    }

    void render() {
        if (!q->isExposed() || q->size().isEmpty())
            return;
        createSwapChain();
        resizeSwapChain();
        updateQuickGeometry();
        renderQuick();
        updateConstants();

        ID3D11RenderTargetView *target = backBufferView.Get();
        context->OMSetRenderTargets(1, &target, nullptr);
        const D3D11_VIEWPORT viewport{
            0.0f, 0.0f,
            static_cast<float>(swapChainSize.width()),
            static_cast<float>(swapChainSize.height()),
            0.0f, 1.0f,
        };
        context->RSSetViewports(1, &viewport);
        context->IASetInputLayout(nullptr);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->VSSetShader(vertexShader.Get(), nullptr, 0);
        context->PSSetShader(pixelShader.Get(), nullptr, 0);
        ID3D11Buffer *constants = constantBuffer.Get();
        context->PSSetConstantBuffers(0, 1, &constants);
        ID3D11ShaderResourceView *texture = uiTextureView.Get();
        context->PSSetShaderResources(0, 1, &texture);
        ID3D11SamplerState *samplerState = sampler.Get();
        context->PSSetSamplers(0, 1, &samplerState);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->OMSetDepthStencilState(nullptr, 0);
        context->RSSetState(nullptr);
        context->Draw(3, 0);

        ID3D11ShaderResourceView *nullView = nullptr;
        context->PSSetShaderResources(0, 1, &nullView);
        swapChain->Present(1, 0);
        q->requestUpdate();
    }

    void markQuickDirty() {
        quickDirty = true;
        q->requestUpdate();
    }

    PresentationWindow *q;
    std::unique_ptr<PresentationOutputState> outputState;
    std::unique_ptr<RenderControl> renderControl;
    std::unique_ptr<QQuickWindow> quickWindow;
    std::unique_ptr<QQmlEngine> qmlEngine;
    std::unique_ptr<QQmlComponent> qmlComponent;
    std::unique_ptr<QQuickItem> rootItem;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID3D11Texture2D> backBuffer;
    ComPtr<ID3D11RenderTargetView> backBufferView;
    ComPtr<ID3D11Texture2D> uiTexture;
    ComPtr<ID3D11ShaderResourceView> uiTextureView;
    ComPtr<ID3D11VertexShader> vertexShader;
    ComPtr<ID3D11PixelShader> pixelShader;
    ComPtr<ID3D11Buffer> constantBuffer;
    ComPtr<ID3D11SamplerState> sampler;
    QSize swapChainSize;
    QSize uiTextureSize;
    bool quickDirty = true;
};

PresentationWindow::PresentationWindow() {
    setTitle(tr("Sunroom — RHI / HDR"));
    resize(1100, 760);
    setMinimumSize({760, 560});
    m_impl = std::make_unique<Impl>(this);
}

PresentationWindow::~PresentationWindow() = default;

void PresentationWindow::exposeEvent(QExposeEvent *) {
    if (isExposed())
        requestUpdate();
}

void PresentationWindow::resizeEvent(QResizeEvent *) {
    if (m_impl)
        m_impl->markQuickDirty();
}

void PresentationWindow::mousePressEvent(QMouseEvent *event) {
    QMouseEvent mapped(
        event->type(), event->position(), event->globalPosition(),
        event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(m_impl->quickWindow.get(), &mapped);
}

void PresentationWindow::mouseReleaseEvent(QMouseEvent *event) {
    QMouseEvent mapped(
        event->type(), event->position(), event->globalPosition(),
        event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(m_impl->quickWindow.get(), &mapped);
}

void PresentationWindow::mouseMoveEvent(QMouseEvent *event) {
    QMouseEvent mapped(
        event->type(), event->position(), event->globalPosition(),
        event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(m_impl->quickWindow.get(), &mapped);
}

void PresentationWindow::wheelEvent(QWheelEvent *event) {
    QCoreApplication::sendEvent(m_impl->quickWindow.get(), event);
}

void PresentationWindow::keyPressEvent(QKeyEvent *event) {
    QCoreApplication::sendEvent(m_impl->quickWindow.get(), event);
}

void PresentationWindow::keyReleaseEvent(QKeyEvent *event) {
    QCoreApplication::sendEvent(m_impl->quickWindow.get(), event);
}

bool PresentationWindow::event(QEvent *event) {
    if (event->type() == QEvent::UpdateRequest) {
        if (m_impl)
            m_impl->render();
        return true;
    }
    return QWindow::event(event);
}
