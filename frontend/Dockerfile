# syntax=docker/dockerfile:1

# ---- build stage -------------------------------------------------------
FROM node:22-slim AS build
WORKDIR /app
COPY package*.json ./
RUN npm install
COPY . .
RUN npm run build

# ---- runtime stage ------------------------------------------------------
# Static bundle served by nginx, which also reverse-proxies /api and /ws to
# the backend container -- replacing Vite's dev-server proxy (vite.config.ts)
# for production, since there's no Vite dev server running here.
FROM nginx:1.27-alpine AS runtime
COPY --from=build /app/dist /usr/share/nginx/html
COPY nginx.conf /etc/nginx/conf.d/default.conf

EXPOSE 80