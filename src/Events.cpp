#include "Manager.h"  
#include "Events.h"
#include "Settings.h"
#include "Hooks.h"
#include "Utils.h"
#include <fstream>
#include <filesystem> 
#include "MCP.h"

constexpr const char* settings_path = "Data/SKSE/Plugins/CycleMovesets/CycleMoveset_Settings.json";

void __stdcall UI::Render() {

    AnimationManager::GetSingleton().DrawMainMenu();  // Chamando a função com o nome correto
}
void __stdcall DrawNPCMenus() {

    AnimationManager::GetSingleton().DrawNPCMenu();  // Chamando a função com o nome correto
}

namespace MyMenu {
    void __stdcall RenderKeybindPage() {
        // Texto traduzido
        ImGui::Text(LOC("settings_description"));
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("SettingsTabs")) {
            // Aba Geral traduzida
            if (ImGui::BeginTabItem(LOC("tab_general"))) {
                ImGui::Spacing();

                bool settings_changed = false;

                // Opção e tooltip traduzidos
                if (ImGui::Checkbox(LOC("option_auto_cycle"), &Settings::CycleMoveset)) {
                    settings_changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(LOC("tooltip_auto_cycle"));
                }

                ImGui::Spacing();
                ImGui::SetNextItemWidth(200.0f);
                // Opção e tooltip traduzidos
                if (ImGui::SliderFloat(LOC("option_cycle_timer"), &Settings::CycleTimer, 0.5f, 5.0f, "%.1f s")) {
                    settings_changed = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(?)");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(LOC("tooltip_cycle_timer"));
                }

                if (settings_changed) {
                    MyMenu::SaveSettings();
                }

                ImGui::EndTabItem();
            }

            // Aba de Teclado traduzida
            if (ImGui::BeginTabItem(LOC("tab_keyboard"))) {
                ImGui::Spacing();
                MyMenu::Keybind(LOC("keybind_stance_menu"), &Settings::hotkey_principal_k);
                MyMenu::Keybind(LOC("keybind_moveset_menu"), &Settings::hotkey_segunda_k);
                MyMenu::Keybind(LOC("keybind_back"), &Settings::hotkey_quarta_k);
                MyMenu::Keybind(LOC("keybind_next"), &Settings::hotkey_terceira_k);
                ImGui::EndTabItem();
            }

            // Aba de Controle traduzida
            if (ImGui::BeginTabItem(LOC("tab_controller"))) {
                ImGui::Spacing();
                MyMenu::GamepadKeybind(LOC("gamepad_stance_menu"), &Settings::hotkey_principal_g);
                MyMenu::GamepadKeybind(LOC("gamepad_moveset_menu"), &Settings::hotkey_segunda_g);
                MyMenu::GamepadKeybind(LOC("gamepad_back"), &Settings::hotkey_quarta_g);
                MyMenu::GamepadKeybind(LOC("gamepad_next"), &Settings::hotkey_terceira_g);
                ImGui::EndTabItem();
            }

            // --- NOVA ABA DE IDIOMA ---
            if (ImGui::BeginTabItem(LOC("tab_language"))) {
                ImGui::Spacing();
                ImGui::Text(LOC("language_select_label"));
                ImGui::SetNextItemWidth(200.0f);

                auto& locManager = LocalizationManager::GetSingleton();
                const char* currentLang = locManager.GetCurrentLanguage().c_str();

                if (ImGui::BeginCombo("##LanguageCombo", currentLang)) {
                    for (const auto& lang : locManager.GetAvailableLanguages()) {
                        const bool is_selected = (currentLang == lang);
                        if (ImGui::Selectable(lang.c_str(), is_selected)) {
                            if (Settings::SelectedLanguage != lang) {
                                Settings::SelectedLanguage = lang;
                                locManager.LoadLanguage(lang);
                                MyMenu::SaveSettings();  // Salva a nova seleção
                            }
                        }
                        if (is_selected) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    void SaveSettings() {
        SKSE::log::info("Salvando configurações...");

        rapidjson::Document doc;
        doc.SetObject();
        rapidjson::Document::AllocatorType& allocator = doc.GetAllocator();

        // --- ADICIONAR SALVAMENTO DO IDIOMA ---
        doc.AddMember("SelectedLanguage", rapidjson::Value(Settings::SelectedLanguage.c_str(), allocator), allocator);

        // Adiciona as configurações gerais
        doc.AddMember("CycleMoveset", Settings::CycleMoveset, allocator);
        doc.AddMember("CycleTimer", Settings::CycleTimer, allocator);

        // Cria o array de dispositivos
        rapidjson::Value devicesArray(rapidjson::kArrayType);

        // --- Dispositivo Teclado ---
        rapidjson::Value keyboardDevice(rapidjson::kObjectType);
        keyboardDevice.AddMember("Device", "Keyboard", allocator);
        rapidjson::Value keyboardKeys(rapidjson::kObjectType);
        keyboardKeys.AddMember("hotkey_principal_k", Settings::hotkey_principal_k, allocator);
        keyboardKeys.AddMember("hotkey_segunda_k", Settings::hotkey_segunda_k, allocator);
        keyboardKeys.AddMember("hotkey_terceira_k", Settings::hotkey_terceira_k, allocator);
        keyboardKeys.AddMember("hotkey_quarta_k", Settings::hotkey_quarta_k, allocator);
        keyboardDevice.AddMember("Keys", keyboardKeys, allocator);
        devicesArray.PushBack(keyboardDevice, allocator);

        // --- Dispositivo Controle ---
        rapidjson::Value controllerDevice(rapidjson::kObjectType);
        controllerDevice.AddMember("Device", "Controller", allocator);
        rapidjson::Value controllerKeys(rapidjson::kObjectType);
        controllerKeys.AddMember("hotkey_principal_g", Settings::hotkey_principal_g, allocator);
        controllerKeys.AddMember("hotkey_segunda_g", Settings::hotkey_segunda_g, allocator);
        controllerKeys.AddMember("hotkey_terceira_g", Settings::hotkey_terceira_g, allocator);
        controllerKeys.AddMember("hotkey_quarta_g", Settings::hotkey_quarta_g, allocator);
        controllerDevice.AddMember("Keys", controllerKeys, allocator);
        devicesArray.PushBack(controllerDevice, allocator);

        doc.AddMember("Devices", devicesArray, allocator);

        // Converte o JSON para uma string e salva no arquivo
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);

        std::filesystem::path config_path(settings_path);
        std::filesystem::create_directories(config_path.parent_path());

        std::ofstream file(settings_path);
        if (file.is_open()) {
            file << buffer.GetString();
            file.close();
            SKSE::log::info("Configurações salvas em {}", settings_path);
        } else {
            SKSE::log::error("Falha ao abrir o arquivo para salvar as configurações: {}", settings_path);
        }
    }

    //  NOVA FUNÇÃO: Carrega as configurações de um arquivo JSON
    void LoadSettings() {
        SKSE::log::info("Carregando configurações...");

        std::ifstream file(settings_path);
        if (!file.is_open()) {
            SKSE::log::info("Arquivo de configurações não encontrado. Usando valores padrão e salvando um novo.");
            SaveSettings();  // Salva um arquivo com os valores padrão na primeira vez
            return;
        }

        std::string json_data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        file.close();

        rapidjson::Document doc;
        doc.Parse(json_data.c_str());

        if (doc.HasParseError() || !doc.IsObject()) {
            SKSE::log::error("Falha ao analisar o arquivo de configurações. Usando valores padrão.");
            return;
        }

        // Carrega as configurações gerais
        // --- ADICIONAR CARREGAMENTO DO IDIOMA ---
        if (doc.HasMember("SelectedLanguage") && doc["SelectedLanguage"].IsString()) {
            Settings::SelectedLanguage = doc["SelectedLanguage"].GetString();
            // Carrega o idioma salvo imediatamente
            LocalizationManager::GetSingleton().LoadLanguage(Settings::SelectedLanguage);
        } else {
            // Se não houver idioma salvo, carrega o padrão
            LocalizationManager::GetSingleton().LoadLanguage("English");
        }

        // Carrega as configurações gerais
        if (doc.HasMember("CycleMoveset") && doc["CycleMoveset"].IsBool()) {
            Settings::CycleMoveset = doc["CycleMoveset"].GetBool();
        }
        if (doc.HasMember("CycleTimer") && doc["CycleTimer"].IsFloat()) {
            Settings::CycleTimer = doc["CycleTimer"].GetFloat();
        }

        // Carrega as configurações dos dispositivos
        if (doc.HasMember("Devices") && doc["Devices"].IsArray()) {
            for (auto& device : doc["Devices"].GetArray()) {
                if (device.IsObject() && device.HasMember("Device") && device["Device"].IsString() &&
                    device.HasMember("Keys") && device["Keys"].IsObject()) {
                    std::string deviceName = device["Device"].GetString();
                    const rapidjson::Value& keys = device["Keys"];

                    if (deviceName == "Keyboard") {
                        if (keys.HasMember("hotkey_principal_k") && keys["hotkey_principal_k"].IsInt())
                            Settings::hotkey_principal_k = keys["hotkey_principal_k"].GetInt();
                        if (keys.HasMember("hotkey_segunda_k") && keys["hotkey_segunda_k"].IsInt())
                            Settings::hotkey_segunda_k = keys["hotkey_segunda_k"].GetInt();
                        if (keys.HasMember("hotkey_terceira_k") && keys["hotkey_terceira_k"].IsInt())
                            Settings::hotkey_terceira_k = keys["hotkey_terceira_k"].GetInt();
                        if (keys.HasMember("hotkey_quarta_k") && keys["hotkey_quarta_k"].IsInt())
                            Settings::hotkey_quarta_k = keys["hotkey_quarta_k"].GetInt();
                    } else if (deviceName == "Controller") {
                        if (keys.HasMember("hotkey_principal_g") && keys["hotkey_principal_g"].IsInt())
                            Settings::hotkey_principal_g = keys["hotkey_principal_g"].GetInt();
                        if (keys.HasMember("hotkey_segunda_g") && keys["hotkey_segunda_g"].IsInt())
                            Settings::hotkey_segunda_g = keys["hotkey_segunda_g"].GetInt();
                        if (keys.HasMember("hotkey_terceira_g") && keys["hotkey_terceira_g"].IsInt())
                            Settings::hotkey_terceira_g = keys["hotkey_terceira_g"].GetInt();
                        if (keys.HasMember("hotkey_quarta_g") && keys["hotkey_quarta_g"].IsInt())
                            Settings::hotkey_quarta_g = keys["hotkey_quarta_g"].GetInt();
                    }
                }
            }
        }

        SKSE::log::info("Configurações carregadas com sucesso.");
        GlobalControl::UpdateRegisteredHotkeys();
    }

    // O CORPO INTEIRO DA FUNÇÃO QUE VOCÊ RECORTOU DE hooks.h VEM PARA CÁ
    void Keybind(const char* label, int* dx_key_ptr) {
        static std::map<const char*, bool> is_waiting_map;
        bool& is_waiting_for_key = is_waiting_map[label];

        // --- LÓGICA DE EXIBIÇÃO ---
        const char* button_text = "[Nenhuma]";
        if (g_dx_to_name_map.count(*dx_key_ptr)) {
            button_text = g_dx_to_name_map.at(*dx_key_ptr);
        }

        if (is_waiting_for_key) {
            button_text = "[ ... ]";
        }
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);
        ImGui::SameLine();
        if (ImGui::Button(button_text, ImVec2(120, 60))) {
            is_waiting_for_key = true;
        }

        // --- LÓGICA DE CAPTURA E CONVERSÃO ---
        if (is_waiting_for_key) {
            // Primeiro, verificamos a tecla Escape para cancelar a atribuição
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                *dx_key_ptr = 0;  // Define como 0 (Nenhuma)
                is_waiting_for_key = false;
            } else {
                // ALTERAÇÃO PRINCIPAL AQUI
                // Itera diretamente sobre o mapa de teclas que você definiu
                for (const auto& pair : g_imgui_to_dx_map) {
                    ImGuiKey key_to_check = pair.first;  // A tecla do ImGui (ex: ImGuiKey_LeftCtrl)
                    int dx_code = pair.second;           // O código DX correspondente (ex: 29)

                    if (ImGui::IsKeyPressed(key_to_check)) {
                        *dx_key_ptr = dx_code;  // Atribui o código DX correto
                        is_waiting_for_key = false;
                        GlobalControl::UpdateRegisteredHotkeys();
                        MyMenu::SaveSettings();
                        break;
                    }
                }
            }
        }
    }
    void GamepadKeybind(const char* label, int* dx_key_ptr) {
        // --- LÓGICA DE EXIBIÇÃO ---
        const char* current_button_name = LOC("keybind_none");
        // Encontra o nome do botão atualmente selecionado para exibi-lo.
        if (g_gamepad_dx_to_name_map.count(*dx_key_ptr)) {
            current_button_name = g_gamepad_dx_to_name_map.at(*dx_key_ptr);
        }

        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", label);
        ImGui::SameLine();

        // Cria um ID único para o Combo para evitar conflitos se houver múltiplos keybinds
        std::string combo_id = "##";
        combo_id += label;

        // --- LÓGICA DO WIDGET COMBO ---
        if (ImGui::BeginCombo(combo_id.c_str(), current_button_name)) {
            // Itera sobre nosso mapa de botões do controle para preencher a lista
            for (const auto& pair : g_gamepad_dx_to_name_map) {
                const int button_code = pair.first;
                const char* button_name = pair.second;

                const bool is_selected = (*dx_key_ptr == button_code);

                // ImGui::Selectable cria um item clicável na lista
                if (ImGui::Selectable(button_name, is_selected)) {
                    // Se o usuário clicou em um item, atualizamos a variável
                    if (*dx_key_ptr != button_code) {
                        *dx_key_ptr = button_code;
                        // Salva as configurações e atualiza os hotkeys registrados no jogo
                        MyMenu::SaveSettings();
                        GlobalControl::UpdateRegisteredHotkeys();
                    }
                }

                // Se o item selecionado é o atual, garante que ele fique visível na lista
                if (is_selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }

}

void UI::RegisterMenu() {
    if (!SKSEMenuFramework::IsInstalled()) {
        SKSE::log::warn("SKSE Menu Framework não encontrado.");
        return;
    }
    SKSE::log::info("SKSE Menu Framework encontrado. Registrando o menu.");
    // --- MUDANÇA DE ORDEM E NOVAS CHAMADAS ---
    // 1. Escaneia quais idiomas existem
    LocalizationManager::GetSingleton().ScanLanguages();
    // 2. Carrega as configurações, que por sua vez carregará o idioma salvo
    MyMenu::LoadSettings();
    SKSEMenuFramework::SetSection("Cycle Movesets");
    // Usa LOC para os nomes dos itens do menu
    SKSEMenuFramework::AddSectionItem(LOC("menu_player"), UI::Render);
    SKSEMenuFramework::AddSectionItem(LOC("menu_npc"), DrawNPCMenus);
    SKSEMenuFramework::AddSectionItem(LOC("menu_settings"), MyMenu::RenderKeybindPage);
}

