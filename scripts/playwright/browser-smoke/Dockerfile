FROM mcr.microsoft.com/playwright:v1.61.0-noble

ENV PLAYWRIGHT_SKIP_BROWSER_DOWNLOAD=1
ENV NODE_PATH=/opt/pek-browser-smoke/node_modules
ENV PATH=/opt/pek-browser-smoke/node_modules/.bin:$PATH

WORKDIR /opt/pek-browser-smoke
COPY .npmrc package.json package-lock.json ./
RUN test "$(node -p "require('./package-lock.json').packages['node_modules/@playwright/test'].version")" = "1.61.0" && \
    npm ci --ignore-scripts

USER pwuser
