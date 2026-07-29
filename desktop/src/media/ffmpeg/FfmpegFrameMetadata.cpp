#include "media/ffmpeg/FfmpegFrameMetadata.h"

#include <cstring>

extern "C" {
#include <libavcodec/codec_par.h>
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
}

namespace {
bool copyStreamSideData(
        AVFrame &frame,
        const AVCodecParameters &parameters,
        enum AVPacketSideDataType packetType,
        enum AVFrameSideDataType frameType) {
    if (av_frame_get_side_data(&frame, frameType))
        return true;

    const AVPacketSideData *source =
        av_packet_side_data_get(
            parameters.coded_side_data,
            parameters.nb_coded_side_data,
            packetType);
    if (!source)
        return true;

    AVFrameSideData *destination =
        av_frame_new_side_data(
            &frame, frameType, source->size);
    if (!destination)
        return false;
    std::memcpy(
        destination->data, source->data, source->size);
    return true;
}
}

bool mergeStreamVideoMetadata(
        AVFrame &frame,
        const AVCodecParameters &parameters) {
    if (frame.color_primaries == AVCOL_PRI_UNSPECIFIED)
        frame.color_primaries = parameters.color_primaries;
    if (frame.color_trc == AVCOL_TRC_UNSPECIFIED)
        frame.color_trc = parameters.color_trc;
    if (frame.colorspace == AVCOL_SPC_UNSPECIFIED)
        frame.colorspace = parameters.color_space;
    if (frame.color_range == AVCOL_RANGE_UNSPECIFIED)
        frame.color_range = parameters.color_range;
    if (frame.chroma_location == AVCHROMA_LOC_UNSPECIFIED) {
        frame.chroma_location =
            parameters.chroma_location;
    }

    return copyStreamSideData(
            frame,
            parameters,
            AV_PKT_DATA_DISPLAYMATRIX,
            AV_FRAME_DATA_DISPLAYMATRIX)
        && copyStreamSideData(
            frame,
            parameters,
            AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
            AV_FRAME_DATA_MASTERING_DISPLAY_METADATA)
        && copyStreamSideData(
            frame,
            parameters,
            AV_PKT_DATA_CONTENT_LIGHT_LEVEL,
            AV_FRAME_DATA_CONTENT_LIGHT_LEVEL)
        && copyStreamSideData(
            frame,
            parameters,
            AV_PKT_DATA_DYNAMIC_HDR10_PLUS,
            AV_FRAME_DATA_DYNAMIC_HDR_PLUS);
}
