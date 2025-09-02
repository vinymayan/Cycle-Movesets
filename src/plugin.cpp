#include "logger.h"
#include "Utils.h"
#include "Events.h"
#include "Manager.h"
#include "Serialization.h"
#include "OARAPI.h"

namespace fs = std::filesystem;

// 2. Declare um ponteiro global para a interface da API.
OAR_API::Animations::IAnimationsInterface* g_oarAPI = nullptr;

// Fun��o para solicitar e obter a interface da API do OAR
// (Esta fun��o � uma c�pia da que existe no pr�prio OAR, para conveni�ncia)
void RequestOAR_API() {
    // O nome do plugin deve ser exato e case-sensitive.
    const auto pluginHandle = GetModuleHandleA("OpenAnimationReplacer.dll");
    if (!pluginHandle) {
        SKSE::log::warn("N�o foi poss�vel encontrar OpenAnimationReplacer.dll. A API n�o estar� dispon�vel.");
        return;
    }

    // O nome da fun��o exportada tamb�m deve ser exato.
    const auto requestAPIFunction = reinterpret_cast<OAR_API::Animations::_RequestPluginAPI_Animations>(
        GetProcAddress(pluginHandle, "RequestPluginAPI_Animations"));
    if (!requestAPIFunction) {
        SKSE::log::warn(
            "N�o foi poss�vel encontrar a fun��o 'RequestPluginAPI_Animations' no OpenAnimationReplacer.dll.");
        return;
    }

    // Obtenha a declara��o do seu pr�prio plugin para passar para a API.
    const auto plugin = SKSE::PluginDeclaration::GetSingleton();
    g_oarAPI = requestAPIFunction(OAR_API::Animations::InterfaceVersion::Latest, plugin->GetName().data(),
                                  plugin->GetVersion());

    if (g_oarAPI) {
        SKSE::log::info("Interface da API do Open Animation Replacer obtida com sucesso.");
    } else {
        SKSE::log::warn("Falha ao obter a interface da API do Open Animation Replacer.");
    }
}

// Esta � a fun��o que voc� chamar� em seu c�digo quando quiser recarregar as anima��es.
bool RecarregarAnimacoesOAR() {
    if (g_oarAPI) {
        SKSE::log::info("[CycleMovesets] API do OAR encontrada. Tentando recarregar anima��es...");
        g_oarAPI->ReloadAnimations();
        SKSE::log::info("[CycleMovesets] Chamada para ReloadAnimations() enviada.");
        return true;  // <-- ALTERA��O: Informa que a chamada foi bem-sucedida
        
    }
    else {
       SKSE::log::error(
        "[CycleMovesets] ERRO: Tentativa de recarregar anima��es, mas a API do OAR � nula (nullptr).");
        return false;  // <-- ALTERA��O: Informa que a chamada falhou

    }
}


void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kInputLoaded) {
    }

    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        RequestOAR_API();

        GlobalControl::g_clientID = SkyPromptAPI::RequestClientID();
        if (GlobalControl::g_clientID > 0) {
            SKSE::log::info("ClientID {} recebido da SkyPromptAPI.", GlobalControl::g_clientID);
            if (!SkyPromptAPI::RequestTheme(GlobalControl::g_clientID, "Cycle Movesets")) {
			    logger::error("Falha ao solicitar o tema 'Cycle Movesets' na SkyPromptAPI.");
            }
        } else {
            SKSE::log::error("Falha ao obter um ClientID da SkyPromptAPI. A API esta instalada?");
        }
    }

    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
        
        // 2. Requisitar um ClientID da API SkyPrompt
        auto* inputDeviceManager = RE::BSInputDeviceManager::GetSingleton();
        if (inputDeviceManager) {
            inputDeviceManager->AddEventSink(InputListener::GetSingleton());
            SKSE::log::info("Listener de input registrado com sucesso!");
        }

        // Em algum lugar na inicializa��o do seu plugin (ex: SKSEPlugin_Load)
        auto* animationEventSource = RE::PlayerCharacter::GetSingleton();
        if (animationEventSource) {
            animationEventSource->AddAnimationGraphEventSink(GlobalControl::AnimationEventHandler::GetSingleton());
            SKSE::log::info("AnimationEventHandler registrado com sucesso.");
        }
        auto* NpcCycle = RE::ScriptEventSourceHolder::GetSingleton();
        if (NpcCycle) {
            NpcCycle->AddEventSink(GlobalControl::NpcCombatTracker::GetSingleton());
            SKSE::log::info("NpcCycleSink (All NPCs) registrado com sucesso.");
        }

        SKSE::GetCameraEventSource()->AddEventSink(GlobalControl::CameraChange::GetSingleton());

        
        if (auto* ui = RE::UI::GetSingleton(); ui) {
            logger::info("Adding event sink for dialogue menu auto zoom.");
            ui->AddEventSink<RE::MenuOpenCloseEvent>(GlobalControl::MenuOpen::GetSingleton());
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    AnimationManager::GetSingleton()->ScanAnimationMods();
    SKSE::Init(skse);
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    
    // Registra seu ouvinte de eventos de A��o (sacar/guardar arma)
    auto* eventSource = SKSE::GetActionEventSource();
    if (eventSource) {
        eventSource->AddEventSink(GlobalControl::ActionEventHandler::GetSingleton());
        SKSE::log::info("Ouvinte de eventos de acao registrado com sucesso!");
    }

     // 1. Escaneia os arquivos de anima��o para carregar os dados.

    // 2. ALTERA��O AQUI: Chame a fun��o para registrar o menu no framework.
    UI::RegisterMenu();



    return true;
}
