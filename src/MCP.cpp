#include <algorithm>
#include <fstream>
#include "MCP.h"
#include "SKSE/SKSE.h"
#include "rapidjson/error/en.h" 

LocalizationManager& LocalizationManager::GetSingleton() {
    static LocalizationManager instance;
    return instance;
}

void LocalizationManager::ScanLanguages() {
    _availableLanguages.clear();
    std::filesystem::path langPath = "Data/SKSE/Plugins/CycleMovesets/Language";

    if (!std::filesystem::exists(langPath) || !std::filesystem::is_directory(langPath)) {
        SKSE::log::warn("Pasta de idiomas n�o encontrada em {}", langPath.string());
        _availableLanguages.push_back("English");  // Adiciona o padr�o
        return;
    }

    SKSE::log::info("Escaneando por arquivos de idioma...");
    for (const auto& entry : std::filesystem::directory_iterator(langPath)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            std::string langName = entry.path().stem().string();
            _availableLanguages.push_back(langName);
            SKSE::log::info(" - Idioma encontrado: {}", langName);
        }
    }

    // Garante que 'English' exista e seja o primeiro da lista, para ser o padr�o do combo box.
    auto it = std::find(_availableLanguages.begin(), _availableLanguages.end(), "English");
    if (it == _availableLanguages.end()) {
        // Se English.json n�o existe, adiciona mesmo assim como op��o.
        _availableLanguages.insert(_availableLanguages.begin(), "English");
    } else if (it != _availableLanguages.begin()) {
        // Se existe mas n�o � o primeiro, move para o in�cio.
        std::iter_swap(_availableLanguages.begin(), it);
    }
}

bool LocalizationManager::LoadLanguage(const std::string& languageName) {
    SKSE::log::info("Tentando carregar o idioma: {}", languageName);

    // 1. Carregar o ingl�s como fallback, se ainda n�o foi carregado
    if (!_englishLoaded) {
        std::filesystem::path englishPath = "Data/SKSE/Plugins/CycleMovesets/Language/English.json";
        std::ifstream englishFile(englishPath);
        if (englishFile.is_open()) {
            std::string jsonContent((std::istreambuf_iterator<char>(englishFile)), std::istreambuf_iterator<char>());
            englishFile.close();

            rapidjson::Document doc;
            doc.Parse(jsonContent.c_str());
            if (!doc.HasParseError() && doc.IsObject()) {
                for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
                    if (it->name.IsString() && it->value.IsString()) {
                        _defaultTranslations[it->name.GetString()] = it->value.GetString();
                    }
                }
                _englishLoaded = true;
                SKSE::log::info("Idioma padr�o 'English' carregado com sucesso.");
            } else {
                // --- CORRIGIDO ---
                SKSE::log::error("Falha ao analisar English.json. Erro: {}", rapidjson::GetParseError_En(doc.GetParseError()));
            }
        } else {
            SKSE::log::warn("English.json n�o encontrado. A localiza��o pode n�o funcionar corretamente.");
        }
    }

    // 2. Limpar tradu��es antigas e definir idioma atual
    _translations.clear();
    _currentLanguage = languageName;

    // 3. Se o idioma for ingl�s, apenas copie o fallback e retorne
    if (languageName == "English") {
        _translations = _defaultTranslations;
        SKSE::log::info("'English' definido como idioma atual.");
        return true;
    }

    // 4. Carregar o arquivo de idioma solicitado
    std::filesystem::path langPath = "Data/SKSE/Plugins/CycleMovesets/Language/" + languageName + ".json";
    std::ifstream langFile(langPath);

    if (!langFile.is_open()) {
        SKSE::log::error("Arquivo de idioma n�o encontrado: {}", langPath.string());
        return false;
    }

    std::string jsonContent((std::istreambuf_iterator<char>(langFile)), std::istreambuf_iterator<char>());
    langFile.close();

    rapidjson::Document doc;
    doc.Parse(jsonContent.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        // --- CORRIGIDO ---
        SKSE::log::error("Falha ao analisar o arquivo de idioma: {}. Erro: {}", langPath.string(), rapidjson::GetParseError_En(doc.GetParseError()));
        return false;
    }

    for (auto it = doc.MemberBegin(); it != doc.MemberEnd(); ++it) {
        if (it->name.IsString() && it->value.IsString()) {
            _translations[it->name.GetString()] = it->value.GetString();
        }
    }

    SKSE::log::info("Idioma '{}' carregado com sucesso.", languageName);
    return true;
}

const char* LocalizationManager::T(const std::string& key) {
    // Tenta encontrar no idioma atual
    auto it = _translations.find(key);
    if (it != _translations.end()) {
        return it->second.c_str();
    }

    // Se n�o encontrar, tenta no idioma padr�o (ingl�s)
    auto it_default = _defaultTranslations.find(key);
    if (it_default != _defaultTranslations.end()) {
        return it_default->second.c_str();
    }

    // Se n�o encontrar em lugar nenhum, loga um aviso e retorna a pr�pria chave
    SKSE::log::warn("Chave de localiza��o n�o encontrada: '{}'", key);
    _missingKeyBuffer[key] = key;  // Armazena a chave para que o ponteiro seja v�lido
    return _missingKeyBuffer[key].c_str();
}

const std::vector<std::string>& LocalizationManager::GetAvailableLanguages() const { return _availableLanguages; }

const std::string& LocalizationManager::GetCurrentLanguage() const { return _currentLanguage; }
