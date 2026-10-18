# Untrusted TLS fixture

`untrusted-server.pem` and `untrusted-server-key.pem` are a self-signed local
server certificate and its deliberately public test key. They are not deployment
credentials. The Jellyfin integration test serves them on loopback with Qt TLS
and requires native FFmpeg HTTPS to reject the untrusted certificate.
