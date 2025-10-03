#include <fstream>
#include <streambuf>
#include <string>
#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/istreamwrapper.h"

// Para logging no SKSE. Se estiver usando CommonLibSSE, o include pode ser diferente.
#include "SKSE/API.h"
#include "Settings.h"

// Uma struct para retornar os valores de forma organizada.
// Usamos -1 como um valor de erro/não encontrado.
struct KeyCodes {
    int keyboard = -1;
    int gamepad = -1;
};

// --- Função Auxiliar Recursiva ---
// Procura por uma entrada de controle que corresponda a um 'id' e 'section' específicos.
const rapidjson::Value* FindKeyEntry(const rapidjson::Value& node, const std::string& targetId,
                                     const std::string& targetSection) {
    // Verifica se o nó atual é o objeto que procuramos
    if (node.IsObject()) {
        if (node.HasMember("ini") && node["ini"].IsObject()) {
            const auto& ini = node["ini"];
            if (ini.HasMember("id") && ini["id"].IsString() && ini.HasMember("section") && ini["section"].IsString()) {
                if (targetId == ini["id"].GetString() && targetSection == ini["section"].GetString()) {
                    return &node;  // Encontramos!
                }
            }
        }
    }

    // Se não for o nó que queremos, procuramos recursivamente em seus filhos
    if (node.IsObject() && node.HasMember("entries") && node["entries"].IsArray()) {
        for (const auto& entry : node["entries"].GetArray()) {
            const rapidjson::Value* result = FindKeyEntry(entry, targetId, targetSection);
            if (result) {
                return result;  // Propaga o resultado encontrado
            }
        }
    } else if (node.IsArray()) {  // Caso a raiz seja um array
        for (const auto& entry : node.GetArray()) {
            const rapidjson::Value* result = FindKeyEntry(entry, targetId, targetSection);
            if (result) {
                return result;  // Propaga o resultado encontrado
            }
        }
    }

    return nullptr;  // Não encontrado neste ramo
}

// --- Função Principal ---
// Lê o arquivo JSON de configurações e extrai os códigos de tecla padrão.
KeyCodes GetDefaultKeyCodes(const std::string& filePath, const std::string& controlId) {
    KeyCodes result;

    std::ifstream fileStream(filePath);
    if (!fileStream.is_open()) {
        SKSE::log::error("Falha ao abrir o arquivo: {}", filePath);
        return result;
    }

    // RapidJSON pode ler diretamente do stream, o que é eficiente
    rapidjson::IStreamWrapper isw(fileStream);
    rapidjson::Document doc;

    // Faz o parse do JSON e verifica por erros
    doc.ParseStream(isw);
    if (doc.HasParseError()) {
        SKSE::log::error("Erro de parse no JSON: {} (Offset: {})", rapidjson::GetParseError_En(doc.GetParseError()),
                         doc.GetErrorOffset());
        return result;
    }

    // A raiz do seu JSON tem um campo "data" que é um array
    if (!doc.HasMember("data") || !doc["data"].IsArray()) {
        SKSE::log::error("JSON inválido: campo 'data' não encontrado ou não é um array.");
        return result;
    }
    const auto& data = doc["data"];

    // 1. Encontrar o código do teclado (Mouse & Keyboard)
    const rapidjson::Value* mkbNode = FindKeyEntry(data, controlId, "InputBindings.MKB");
    if (mkbNode) {
        if (mkbNode->HasMember("default") && (*mkbNode)["default"].IsInt()) {
            result.keyboard = (*mkbNode)["default"].GetInt();
            SKSE::log::info("Código de tecla MKB para '{}' encontrado: {}", controlId, result.keyboard);
        } else {
            SKSE::log::warn("Nó MKB para '{}' encontrado, mas falta o campo 'default' ou não é um inteiro.", controlId);
        }
    } else {
        SKSE::log::warn("Não foi possível encontrar a entrada de controle MKB para '{}'", controlId);
    }

    // 2. Encontrar o código do controle (Gamepad)
    const rapidjson::Value* gamepadNode = FindKeyEntry(data, controlId, "InputBindings.GamePad");
    if (gamepadNode) {
        if (gamepadNode->HasMember("default") && (*gamepadNode)["default"].IsInt()) {
            result.gamepad = (*gamepadNode)["default"].GetInt();
            SKSE::log::info("Código de tecla Gamepad para '{}' encontrado: {}", controlId, result.gamepad);
        } else {
            SKSE::log::warn("Nó Gamepad para '{}' encontrado, mas falta o campo 'default' ou não é um inteiro.",
                            controlId);
        }
    } else {
        SKSE::log::warn("Não foi possível encontrar a entrada de controle Gamepad para '{}'", controlId);
    }

    return result;
}

// --- Exemplo de como usar a função ---
void WheelerKeys() {
    std::string jsonFilePath = "Data\\SKSE\\Plugins\\dmenu\\customSettings\\Wheeler Controls.json";

    std::string controlIdToFind = "toggleWheel";  // O ID da ação que queremos (ex: Toggle Wheel)

    KeyCodes codes = GetDefaultKeyCodes(jsonFilePath, controlIdToFind);

    if (codes.keyboard != -1) {
        // Agora você tem o valor em uma variável
        WheelerKeyboard = codes.keyboard;
        SKSE::log::info("Variável MKB_ToggleWheelKey definida como: {}", WheelerKeyboard);
        // Use a variável MKB_ToggleWheelKey no resto do seu código...
    } else {
        SKSE::log::error("Não foi possível obter o código da tecla do teclado.");
    }

    if (codes.gamepad != -1) {
        // E aqui o valor do controle
        WheelerGamepad = codes.gamepad;
        SKSE::log::info("Variável Gamepad_ToggleWheelKey definida como: {}", WheelerGamepad);
        // Use a variável Gamepad_ToggleWheelKey no resto do seu código...
    } else {
        SKSE::log::error("Não foi possível obter o código da tecla do controle.");
    }
}