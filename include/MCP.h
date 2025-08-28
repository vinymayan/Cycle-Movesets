#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "rapidjson/document.h"

class LocalizationManager {
public:
    // Padr�o Singleton para ter acesso global
    static LocalizationManager& GetSingleton();

    // Encontra todos os arquivos .json na pasta de idiomas
    void ScanLanguages();

    // Carrega um idioma espec�fico a partir do nome do arquivo (ex: "Brazilian")
    bool LoadLanguage(const std::string& languageName);

    // A fun��o principal que usaremos para obter o texto traduzido
    const char* T(const std::string& key);

    // Fun��es para a UI poder listar os idiomas e saber qual est� selecionado
    const std::vector<std::string>& GetAvailableLanguages() const;
    const std::string& GetCurrentLanguage() const;

private:
    // Construtores privados para o padr�o Singleton
    LocalizationManager() = default;
    ~LocalizationManager() = default;
    LocalizationManager(const LocalizationManager&) = delete;
    LocalizationManager& operator=(const LocalizationManager&) = delete;

    // Armazena as tradu��es do idioma atual (ex: "tab_general" -> "Geral")
    std::map<std::string, std::string> _translations;
    // Armazena as tradu��es do ingl�s como fallback
    std::map<std::string, std::string> _defaultTranslations;
    // Armazena os nomes dos idiomas encontrados (ex: "English", "Brazilian")
    std::vector<std::string> _availableLanguages;

    std::string _currentLanguage = "English";
    bool _englishLoaded = false;

    // Um truque para retornar chaves n�o encontradas sem corromper a mem�ria
    std::map<std::string, std::string> _missingKeyBuffer;
};

// Macro para facilitar a chamada da fun��o de tradu��o no c�digo
#define LOC(key) LocalizationManager::GetSingleton().T(key)