#pragma once

namespace UI {
    // Função para registrar nosso menu no SKSE Menu Framework.
    // Será chamada no carregamento do plugin.
    void RegisterMenu();

    // A função que o SKSE Menu Framework chamará para desenhar nosso menu.
    // O `__stdcall` é uma convenção de chamada que a API do Windows (e o framework) espera.
    void __stdcall Render();
}