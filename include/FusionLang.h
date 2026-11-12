// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

// Instaluje wbudowana polska paczke jezykowa (/pl.lang.json) w LittleFS,
// jesli jej brak albo jest starsza wersja. Wywolac przed I18n.init().
namespace FusionLang {
void install();
}
