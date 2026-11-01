// SSYmirPS5's title IDs (ProsperoMigrate.h): the current one and the ones it used before.
//
// SPDX-License-Identifier: MIT
#pragma once

#ifndef YMIR_TITLE_ID
#define YMIR_TITLE_ID "PPSA99378"
#endif

#define MIG_APP_NAME "SSYmirPS5"
#define MIG_NEW_TITLE YMIR_TITLE_ID
// 1.0-1.6 were PPSA99011, which the homebrew catalog lists for another app (PS5 FSR4 Showcase)
#define MIG_OLD_TITLES "PPSA99379"
// the label of our content ID (UP9000-<title>_00-<label>), the same under every title ID
#define MIG_CONTENT_LABEL "YMIRSATURNPS5001"
// the host tests' override: <prefix>PS5_HOMEBREW
#define MIG_ENV_PREFIX "YMIR_"
