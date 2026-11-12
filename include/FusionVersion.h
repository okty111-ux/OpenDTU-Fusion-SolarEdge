// SPDX-License-Identifier: GPL-2.0-or-later
// Fusion firmware version. Every GitHub release is tagged FUSION_TAG_PREFIX + FUSION_VERSION
// (e.g. "fusion-v2") and the device compares this integer to the newest release.
#pragma once

#ifndef FUSION_VERSION
#define FUSION_VERSION 3
#endif
#ifndef FUSION_TAG_PREFIX
#define FUSION_TAG_PREFIX "fusion-v"
#endif
#ifndef FUSION_UPDATE_REPO
#define FUSION_UPDATE_REPO "okty111-ux/OpenDTU-Fusion-SolarEdge"
#endif
#ifndef PIOENV
#define PIOENV "opendtufusionv2"
#endif
// firmware asset name inside a release: fusion-<pio env>.bin
#define FUSION_ASSET_NAME "fusion-" PIOENV ".bin"
