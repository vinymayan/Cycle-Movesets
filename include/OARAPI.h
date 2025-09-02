#pragma once

#include "OAR/OpenAnimationReplacerAPI-Animations.h"

// 1. Declare o ponteiro da API como 'extern'.
// Isso informa ao compilador: "Esta vari�vel existe, mas est� definida em outro arquivo .cpp".
extern OAR_API::Animations::IAnimationsInterface* g_oarAPI;

// 2. Declare a sua fun��o helper tamb�m.
// Assim, qualquer arquivo que inclua este header saber� que a fun��o existe.
bool RecarregarAnimacoesOAR();