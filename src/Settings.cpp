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
// Usamos -1 como um valor de erro/n�o encontrado.
struct KeyCodes {
    int keyboard = -1;
    int gamepad = -1;
};

// --- Fun��o Auxiliar Recursiva ---
// Procura por uma entrada de controle que corresponda a um 'id' e 'section' espec�ficos.
const rapidjson::Value* FindKeyEntry(const rapidjson::Value& node, const std::string& targetId,
                                     const std::string& targetSection) {
    // Verifica se o n� atual � o objeto que procuramos
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

    // Se n�o for o n� que queremos, procuramos recursivamente em seus filhos
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

    return nullptr;  // N�o encontrado neste ramo
}

// --- Fun��o Principal ---
// L� o arquivo JSON de configura��es e extrai os c�digos de tecla padr�o.
KeyCodes GetDefaultKeyCodes(const std::string& filePath, const std::string& controlId) {
    KeyCodes result;

    std::ifstream fileStream(filePath);
    if (!fileStream.is_open()) {
        SKSE::log::error("Falha ao abrir o arquivo: {}", filePath);
        return result;
    }

    // RapidJSON pode ler diretamente do stream, o que � eficiente
    rapidjson::IStreamWrapper isw(fileStream);
    rapidjson::Document doc;

    // Faz o parse do JSON e verifica por erros
    doc.ParseStream(isw);
    if (doc.HasParseError()) {
        SKSE::log::error("Erro de parse no JSON: {} (Offset: {})", rapidjson::GetParseError_En(doc.GetParseError()),
                         doc.GetErrorOffset());
        return result;
    }

    // A raiz do seu JSON tem um campo "data" que � um array
    if (!doc.HasMember("data") || !doc["data"].IsArray()) {
        SKSE::log::error("JSON inv�lido: campo 'data' n�o encontrado ou n�o � um array.");
        return result;
    }
    const auto& data = doc["data"];

    // 1. Encontrar o c�digo do teclado (Mouse & Keyboard)
    const rapidjson::Value* mkbNode = FindKeyEntry(data, controlId, "InputBindings.MKB");
    if (mkbNode) {
        if (mkbNode->HasMember("default") && (*mkbNode)["default"].IsInt()) {
            result.keyboard = (*mkbNode)["default"].GetInt();
            SKSE::log::info("C�digo de tecla MKB para '{}' encontrado: {}", controlId, result.keyboard);
        } else {
            SKSE::log::warn("N� MKB para '{}' encontrado, mas falta o campo 'default' ou n�o � um inteiro.", controlId);
        }
    } else {
        SKSE::log::warn("N�o foi poss�vel encontrar a entrada de controle MKB para '{}'", controlId);
    }

    // 2. Encontrar o c�digo do controle (Gamepad)
    const rapidjson::Value* gamepadNode = FindKeyEntry(data, controlId, "InputBindings.GamePad");
    if (gamepadNode) {
        if (gamepadNode->HasMember("default") && (*gamepadNode)["default"].IsInt()) {
            result.gamepad = (*gamepadNode)["default"].GetInt();
            SKSE::log::info("C�digo de tecla Gamepad para '{}' encontrado: {}", controlId, result.gamepad);
        } else {
            SKSE::log::warn("N� Gamepad para '{}' encontrado, mas falta o campo 'default' ou n�o � um inteiro.",
                            controlId);
        }
    } else {
        SKSE::log::warn("N�o foi poss�vel encontrar a entrada de controle Gamepad para '{}'", controlId);
    }

    return result;
}

// --- Exemplo de como usar a fun��o ---
void WheelerKeys() {
    std::string jsonFilePath = "Data\\SKSE\\Plugins\\dmenu\\customSettings\\Wheeler Controls.json";

    std::string controlIdToFind = "toggleWheel";  // O ID da a��o que queremos (ex: Toggle Wheel)

    KeyCodes codes = GetDefaultKeyCodes(jsonFilePath, controlIdToFind);

    if (codes.keyboard != -1) {
        // Agora voc� tem o valor em uma vari�vel
        WheelerKeyboard = codes.keyboard;
        SKSE::log::info("Vari�vel MKB_ToggleWheelKey definida como: {}", WheelerKeyboard);
        // Use a vari�vel MKB_ToggleWheelKey no resto do seu c�digo...
    } else {
        SKSE::log::error("N�o foi poss�vel obter o c�digo da tecla do teclado.");
    }

    if (codes.gamepad != -1) {
        // E aqui o valor do controle
        WheelerGamepad = codes.gamepad;
        SKSE::log::info("Vari�vel Gamepad_ToggleWheelKey definida como: {}", WheelerGamepad);
        // Use a vari�vel Gamepad_ToggleWheelKey no resto do seu c�digo...
    } else {
        SKSE::log::error("N�o foi poss�vel obter o c�digo da tecla do controle.");
    }
}