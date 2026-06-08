# kmscon-hangul

[![Build Status](https://github.com/johnhsb/kmscon-hangul/actions/workflows/meson.yml/badge.svg?branch=main)](https://github.com/johnhsb/kmscon-hangul/actions/workflows/meson.yml)
[![Release](https://img.shields.io/github/v/release/johnhsb/kmscon-hangul)](https://github.com/johnhsb/kmscon-hangul/releases/latest)

[kmscon](https://github.com/kmscon/kmscon) v10.0.0 기반 한글 입력 지원 및 버그 수정 포크.

Kmscon is a simple terminal emulator based on linux kernel mode setting (KMS).
It is an attempt to replace the in-kernel VT implementation with a userspace console.

## 변경 사항 (upstream 대비)

### 한글 입력 지원
- 범용 IM 추상화 레이어 (`kmscon_im_ops` vtable) 추가 — 향후 다른 입력기 추가 가능
- libhangul 기반 한글 입력 백엔드 (`im_hangul`) — 두벌식/세벌식 지원
- 신규 옵션:
  - `--im-engine=hangul` : 한글 입력기 활성화
  - `--im-params=2` : 키보드 레이아웃 선택 (`2`=두벌식, `32`/`3f`/`3s`=세벌식 등)
  - `--grab-im-toggle=<keysym>` : 한/영 전환 키 설정

### 버그 수정
- `video/drm`: `perform_modeset()`의 use-after-free SEGV 수정 (VT 전환/DPMS 복귀 시 크래시)
- `font/freetype`: CJK 폰트 크기 오류 수정 — `y_ppem` 기준으로 폰트 크기 계산하여 한글 글자가 라틴 문자 대비 과도하게 크게 표시되던 문제 해결

### 패키징
- Debian 패키지 (`kmscon-hangul`) 지원 — libtsm 정적 링크 포함
- GitHub Actions 자동 빌드: amd64 / arm64 .deb 패키지

## 설치 (Debian trixie)

```bash
# GitHub Releases에서 다운로드
sudo apt install ./kmscon-hangul_*_arm64.deb   # ARM64
sudo apt install ./kmscon-hangul_*_amd64.deb   # x86_64
```

최신 릴리즈: https://github.com/johnhsb/kmscon-hangul/releases/latest

## 한글 입력 설정

```bash
sudo mkdir -p /etc/kmscon
cat | sudo tee /etc/kmscon/kmscon.conf << 'EOF'
im-engine=hangul
im-params=2
EOF
```

또는 서비스 실행 시 옵션 직접 지정:
```bash
kmscon --im-engine=hangul --im-params=2
```

## systemd 서비스 설정

```bash
# tty1에서 부팅 시 kmscon 자동 시작
systemctl enable kmsconvt@tty1.service
```

## 빌드 (소스에서)

### 의존성 설치 (Debian/Ubuntu)

```bash
sudo apt install \
  build-essential meson pkg-config python3 \
  libxkbcommon-dev libudev-dev libdrm-dev \
  libgbm-dev libegl-dev libgles-dev \
  libfreetype-dev libfontconfig-dev \
  libpango1.0-dev libsystemd-dev \
  libdbus-1-dev libhangul-dev
```

### 빌드

```bash
git clone https://github.com/johnhsb/kmscon-hangul.git
cd kmscon-hangul
git clone --depth 1 --branch v4.5.0 https://github.com/kmscon/libtsm subprojects/libtsm
meson setup builddir/ \
  --prefix=/usr \
  -Dim_hangul=enabled \
  -Dlibtsm:default_library=static \
  -Dlibtsm:tests=false \
  --force-fallback-for=libtsm
meson install -C builddir/
```

### Debian 패키지 빌드

```bash
sudo apt install debhelper devscripts
git clone --depth 1 --branch v4.5.0 https://github.com/kmscon/libtsm subprojects/libtsm
dpkg-buildpackage -us -uc -b
```

## 빌드 옵션

| 옵션 | 기본값 | 설명 |
|:-----|:------:|:-----|
| `im_hangul` | `auto` | libhangul 한글 입력 백엔드 |
| `extra_debug` | `false` | 추가 디버그 출력 |
| `libseat` | `auto` | libseat 기반 장치 접근 |
| `video_fbdev` | `auto` | Linux fbdev 비디오 백엔드 |
| `video_drm2d` | `auto` | Linux DRM 소프트웨어 렌더링 |
| `video_drm3d` | `auto` | Linux DRM 하드웨어 렌더링 |
| `font_unifont` | `auto` | 내장 유니폰트 |
| `font_freetype` | `auto` | Freetype2 폰트 렌더러 |
| `font_pango` | `auto` | Pango 폰트 렌더러 |
| `renderer_gltex` | `auto` | OpenGLESv2 가속 렌더러 |
| `docs` | `auto` | 맨페이지 및 문서 빌드 |

## Requirements

### Mandatory
- [libtsm](https://github.com/kmscon/libtsm) v4.5.0 (정적 링크, 별도 설치 불필요)
- [libudev](https://www.freedesktop.org/software/systemd/man/libudev.html) >= v172
- [libxkbcommon](https://xkbcommon.org/)
- **linux-headers**

### Optional
- [libhangul](https://github.com/libhangul/libhangul): 한글 입력 (`--im-engine=hangul`)
- [libdrm](https://gitlab.freedesktop.org/mesa/drm): DRM/KMS 비디오
- **OpenGLES2**: 하드웨어 가속 비디오 (libdrm + libgbm + egl + glesv2)
- [freetype](https://freetype.org/): 폰트 렌더링
- [pango](https://gitlab.gnome.org/GNOME/pango): Pango 폰트 렌더링
- [libseat](https://sr.ht/~kennylevinsen/seatd/): 비root 실행

## Config file

기본 설정 파일 경로: `/etc/kmscon/kmscon.conf`

모든 커맨드라인 옵션을 `--` 없이 설정 파일에 기재할 수 있습니다.
예시: [kmscon.conf](scripts/etc/kmscon.conf.example)

## License

MIT License. 자세한 내용은 [`COPYING`](./COPYING) 참조.

## Upstream

이 저장소는 [kmscon/kmscon](https://github.com/kmscon/kmscon) v10.0.0을 기반으로 합니다.
