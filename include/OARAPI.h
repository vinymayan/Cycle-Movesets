#pragma once

#include "OAR/OpenAnimationReplacerAPI-Animations.h"

// 1. Declare o ponteiro da API como 'extern'.
// Isso informa ao compilador: "Esta variável existe, mas está definida em outro arquivo .cpp".
extern OAR_API::Animations::IAnimationsInterface* g_oarAPI;

// 2. Declare a sua função helper também.
// Assim, qualquer arquivo que inclua este header saberá que a função existe.
bool RecarregarAnimacoesOAR();