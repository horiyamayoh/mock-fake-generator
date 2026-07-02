FROM ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54 AS build

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		build-essential \
		ca-certificates \
		clang-18 \
		cmake \
		git \
		libclang-18-dev \
		llvm-18-dev \
		ninja-build \
		python3 \
	&& rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . /src

RUN cmake -S /src -B /build -G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX=/opt/mockfakegen \
		-DMOCKFAKEGEN_BUILD_TESTS=OFF \
		-DMOCKFAKEGEN_ENABLE_FORMAT_TARGETS=OFF \
		-DMOCKFAKEGEN_ENABLE_STRICT_WARNINGS=ON \
		-DMOCKFAKEGEN_WARNINGS_AS_ERRORS=OFF \
		-DMOCKFAKEGEN_LLVM_ROOT=/usr/lib/llvm-18 \
	&& cmake --build /build --target mockfakegen \
	&& cmake --install /build \
	&& strip /opt/mockfakegen/bin/mockfakegen

FROM ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
ARG IMAGE_REVISION=unknown

LABEL org.opencontainers.image.source="https://github.com/horiyamayoh/mock-fake-generator" \
	org.opencontainers.image.description="mockfakegen C++23 header-to-gMock fake generator with LLVM 18 runtime" \
	org.opencontainers.image.revision="${IMAGE_REVISION}" \
	org.opencontainers.image.version="0.1.0" \
	org.opencontainers.image.base.name="ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54"

RUN apt-get update \
	&& apt-get install -y --no-install-recommends \
		ca-certificates \
		clang-18 \
		libgmock-dev \
		libgtest-dev \
		libllvm18 \
	&& rm -rf /var/lib/apt/lists/* \
	&& useradd --create-home --uid 10001 --shell /usr/sbin/nologin mockfakegen

COPY --from=build /opt/mockfakegen /opt/mockfakegen
COPY docker/mockfakegen-entrypoint.sh /usr/local/bin/mockfakegen-entrypoint

RUN chmod 0755 /usr/local/bin/mockfakegen-entrypoint \
	&& printf '{\n  "base_image": "ubuntu:24.04@sha256:786a8b558f7be160c6c8c4a54f9a57274f3b4fb1491cf65146521ae77ff1dc54",\n  "llvm_major": "18",\n  "ubuntu_release": "24.04",\n  "revision": "%s"\n}\n' "${IMAGE_REVISION}" > /etc/mockfakegen-image.json

ENV PATH="/opt/mockfakegen/bin:${PATH}" \
	MOCKFAKEGEN_CXX_COMPILER="/usr/bin/clang++-18" \
	MOCKFAKEGEN_GMOCK_INCLUDE_DIRS="/usr/include"

USER mockfakegen
WORKDIR /workspace

RUN mockfakegen --help >/tmp/mockfakegen-help.txt \
	&& mkdir -p /tmp/mockfakegen-smoke/include /tmp/mockfakegen-smoke/src /tmp/mockfakegen-smoke/build /tmp/mockfakegen-smoke/out \
	&& printf '%s\n' '#pragma once' 'class DockerSmoke { public: int Value(); };' > /tmp/mockfakegen-smoke/include/DockerSmoke.h \
	&& printf '%s\n' '#include "DockerSmoke.h"' > /tmp/mockfakegen-smoke/src/DockerSmoke.cpp \
	&& printf '[{"directory":"/tmp/mockfakegen-smoke","file":"/tmp/mockfakegen-smoke/src/DockerSmoke.cpp","arguments":["/usr/bin/clang++-18","-std=c++23","-I/tmp/mockfakegen-smoke/include","-c","/tmp/mockfakegen-smoke/src/DockerSmoke.cpp"]}]\n' > /tmp/mockfakegen-smoke/build/compile_commands.json \
	&& mockfakegen \
		--input-root /tmp/mockfakegen-smoke/include \
		--output-dir /tmp/mockfakegen-smoke/out \
		--build-path /tmp/mockfakegen-smoke/build \
		--project-root /tmp/mockfakegen-smoke \
		--overwrite \
		--validate compile \
	&& test -f /tmp/mockfakegen-smoke/out/MockDockerSmoke.h \
	&& test -f /tmp/mockfakegen-smoke/out/FakeDockerSmoke.cpp \
	&& ! grep -R -E 'ket::|ket_' /tmp/mockfakegen-smoke/out

ENTRYPOINT ["mockfakegen-entrypoint"]
CMD ["--help"]
