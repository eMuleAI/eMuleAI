FROM debian:trixie-slim

ARG EMULEAI_VERSION=development

LABEL org.opencontainers.image.title="eMule AI"
LABEL org.opencontainers.image.description="eMule AI running with Wine, Xpra seamless windows and optional browser-based noVNC access"
LABEL org.opencontainers.image.version="${EMULEAI_VERSION}"
LABEL org.opencontainers.image.url="https://hub.docker.com/r/emuleai/emuleai"
LABEL org.opencontainers.image.source="https://github.com/eMuleAI/eMuleAI"

ENV DEBIAN_FRONTEND=noninteractive \
    DISPLAY=:0 \
    DISPLAY_MODE=xpra \
    DISPLAY_WIDTH=1280 \
    DISPLAY_HEIGHT=800 \
    XPRA_CANVAS_WIDTH=7680 \
    XPRA_CANVAS_HEIGHT=4320 \
    DISPLAY_DEPTH=24 \
    NOVNC_PORT=6080 \
    XPRA_PORT=14500 \
    EMULE_TCP_PORT=4662 \
    EMULE_UDP_PORT=4672 \
    PUID=1000 \
    PGID=1000 \
    HOME=/root \
    WINEARCH=win64 \
    WINEPREFIX=/data/wineprefix \
    WINEDEBUG=-all \
    WINEDLLOVERRIDES=mscoree,mshtml= \
    LANG=C.UTF-8 \
    LC_ALL=C.UTF-8

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        ca-certificates \
        gnupg \
        wget && \
    wget -qO /usr/share/keyrings/xpra.asc https://xpra.org/xpra.asc && \
    gpg --show-keys --with-colons /usr/share/keyrings/xpra.asc | grep -q 'fpr:::::::::B4993B57323148E37977E5D873254CAD17978FAF:' && \
    wget -qO /etc/apt/sources.list.d/xpra.sources https://raw.githubusercontent.com/Xpra-org/xpra/master/packaging/repos/trixie/xpra.sources && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        fonts-dejavu-core \
        fonts-liberation \
        fonts-noto-cjk \
        gosu \
        novnc \
        openbox \
        python3-minimal \
        tini \
        websockify \
        wine \
        wine64 \
        x11vnc \
        xdotool \
        xpra \
        xpra-x11 \
        xvfb && \
    rm -rf /var/lib/apt/lists/* && \
    groupadd --gid 1000 emuleai && \
    useradd --uid 1000 --gid 1000 --create-home --shell /bin/bash emuleai && \
    ln -sf /usr/share/novnc/vnc.html /usr/share/novnc/index.html

ENV HOME=/home/emuleai

COPY --chown=emuleai:emuleai app/ /opt/emuleai/
COPY --chown=root:root entrypoint.sh /usr/local/bin/emuleai-entrypoint
COPY --chown=root:root configure-settings.py /usr/local/bin/configure-settings.py

RUN test -f /opt/emuleai/eMuleAI.exe && \
    command -v xpra >/dev/null && \
    dpkg-query -W -f='${Status}\n' xpra-x11 | grep -qx 'install ok installed' && \
    sed -i 's/\r$//' /usr/local/bin/emuleai-entrypoint /usr/local/bin/configure-settings.py && \
    mkdir -p /opt/emuleai-default/config && \
    if [ -d /opt/emuleai/config ]; then cp -a /opt/emuleai/config/. /opt/emuleai-default/config/; fi && \
    chmod 0755 /usr/local/bin/emuleai-entrypoint /usr/local/bin/configure-settings.py && \
    chown -R emuleai:emuleai /opt/emuleai /opt/emuleai-default

WORKDIR /opt/emuleai

VOLUME ["/data"]

EXPOSE 6080/tcp 14500/tcp 4662/tcp 4672/udp

STOPSIGNAL SIGTERM

ENTRYPOINT ["/usr/bin/tini", "--", "/usr/local/bin/emuleai-entrypoint"]
