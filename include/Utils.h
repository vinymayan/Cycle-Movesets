#pragma once

#include "ClibUtil/singleton.hpp"
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"
#include "SkyPrompt/API.hpp"
#include "Hooks.h"
#include <chrono>
#include <shared_mutex> // Para acesso seguro ao set

namespace GlobalControl {
    // --- CONFIGURAÇÃO ---
    // Altere estes valores para corresponder à sua variável global no Creation Kit
    //constexpr std::string_view ESP_NAME = "CycleMoveset.esp";
    //constexpr std::string_view GLOBAL_EDITOR_ID = "NovoJeito";

    // Ponteiro para a variável global, será preenchido quando o jogo carregar
    //inline RE::TESGlobal* g_targetGlobal = nullptr;

    inline int g_currentStance = 0;
    inline int g_currentMoveset = 0;
    extern int g_directionalState; 
    // ID do nosso plugin com a API SkyPrompt
    inline SkyPromptAPI::ClientID g_clientID = 0;
    inline bool g_isWeaponDrawn = false;
    inline bool Cycleopen = false;
    inline bool MovesetChangesOpen = false;
    inline bool StanceChangesOpen = false;
    
  
    inline std::string StanceText = "Stances";
    inline std::string MovesetText = "Movesets";
    inline std::string StanceNextText = "Next";
    inline std::string StanceBackText = "Back";
    inline std::string MovesetNextText = "Next";
    inline std::string MovesetBackText = "Back";
    inline std::vector<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>> Stances_menu = {
        {RE::INPUT_DEVICE::kKeyboard, Settings::hotkey_principal_k},
        {RE::INPUT_DEVICE::kGamepad, Settings::hotkey_principal_g}
    };

    inline std::vector<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>> Moveset_menu = {
        {RE::INPUT_DEVICE::kKeyboard, Settings::hotkey_segunda_k},
        {RE::INPUT_DEVICE::kGamepad, Settings::hotkey_segunda_g}
    };
    inline std::vector<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>> Next_key = {
        {RE::INPUT_DEVICE::kKeyboard, Settings::hotkey_terceira_k},
        {RE::INPUT_DEVICE::kGamepad, Settings::hotkey_terceira_k}
    };
    inline std::vector<std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID>> Back_key = {
        {RE::INPUT_DEVICE::kKeyboard, Settings::hotkey_quarta_k},
        {RE::INPUT_DEVICE::kGamepad, Settings::hotkey_quarta_g}
    };
    const std::pair<RE::INPUT_DEVICE, SkyPromptAPI::ButtonID> skyrim_key = {RE::INPUT_DEVICE::kKeyboard, 286}; 
    



    inline SkyPromptAPI::Prompt menu_stance(StanceText, 0, 0, SkyPromptAPI::PromptType::kHoldAndKeep, 20, Stances_menu);
    inline SkyPromptAPI::Prompt stance_actual(StanceText, 0, 0, SkyPromptAPI::PromptType::kSinglePress, 20,
                                              Stances_menu, 0xFFFFFFFF, 0.999f);

    inline SkyPromptAPI::Prompt stance_next(StanceNextText, 3, 0, SkyPromptAPI::PromptType::kSinglePress, 20, Next_key);
 
    inline SkyPromptAPI::Prompt stance_back(StanceBackText, 2, 0, SkyPromptAPI::PromptType::kSinglePress, 20, Back_key);
    
    inline SkyPromptAPI::Prompt menu_moveset(MovesetText, 1, 0, SkyPromptAPI::PromptType::kHoldAndKeep, 20,Moveset_menu);
    inline SkyPromptAPI::Prompt moveset_actual(MovesetText, 1, 0, SkyPromptAPI::PromptType::kSinglePress, 20,
                                               Moveset_menu, 0xFFFFFFFF, 0.999f);
    inline SkyPromptAPI::Prompt moveset_next(MovesetNextText, 3, 0, SkyPromptAPI::PromptType::kSinglePress, 20,
                                             Next_key);

    inline SkyPromptAPI::Prompt moveset_back(MovesetBackText, 2, 0, SkyPromptAPI::PromptType::kSinglePress, 20,
                                             Back_key);

    
    // A definimos como 'inline' aqui mesmo para simplificar e evitar problemas de linker.
    inline void UpdateRegisteredHotkeys() {
        SKSE::log::info("Atualizando hotkeys registradas na SkyPromptAPI...");

        // Atribui o novo valor do scan code (a segunda parte do 'pair')
        Stances_menu[0].second = Settings::hotkey_principal_k;
        Stances_menu[1].second = Settings::hotkey_principal_g;
        Moveset_menu[0].second = Settings::hotkey_segunda_k;
        Moveset_menu[1].second = Settings::hotkey_segunda_g;
        Next_key[0].second = Settings::hotkey_terceira_k;
        Next_key[1].second = Settings::hotkey_terceira_g;
        Back_key[0].second = Settings::hotkey_quarta_k;
        Back_key[1].second = Settings::hotkey_quarta_g;

    }


    // A classe Sink processa os eventos da API
    class StancesSink final : public SkyPromptAPI::PromptSink, public clib_util::singleton::ISingleton<StancesSink> {
    public:
        // Retorna a lista de prompts que queremos monitorar
        std::span<const SkyPromptAPI::Prompt> GetPrompts() const override;

        // Função chamada quando um evento (ex: pressionar tecla) ocorre
        void ProcessEvent(SkyPromptAPI::PromptEvent event) const override;
        mutable bool except = false;
        void UpdatePrompts() { prompts[0] = menu_stance; }

    private:
        // Um array para guardar todos os nossos prompts
        std::array<SkyPromptAPI::Prompt, 1> prompts = {menu_stance};
        
    };

        class StancesChangesSink final : public SkyPromptAPI::PromptSink,
                                     public clib_util::singleton::ISingleton<StancesChangesSink> {

    public:
        // Retorna a lista de prompts que queremos monitorar
        std::span<const SkyPromptAPI::Prompt> GetPrompts() const override;

        // Função chamada quando um evento (ex: pressionar tecla) ocorre
        void ProcessEvent(SkyPromptAPI::PromptEvent event) const override;
        mutable bool except = false;
        void UpdatePrompts() {
            prompts[0] = stance_actual;
            prompts[1] = stance_next;
            prompts[2] = stance_back;
        }
    private:
        // Um array para guardar todos os nossos prompts
        std::array<SkyPromptAPI::Prompt, 3> prompts = {stance_actual,stance_next, stance_back};
    };

            // A classe Sink processa os eventos da API
    class MovesetSink final : public SkyPromptAPI::PromptSink, public clib_util::singleton::ISingleton<MovesetSink> {
    public:
        // Retorna a lista de prompts que queremos monitorar
        std::span<const SkyPromptAPI::Prompt> GetPrompts() const override;

        // Função chamada quando um evento (ex: pressionar tecla) ocorre
        void ProcessEvent(SkyPromptAPI::PromptEvent event) const override;
        mutable bool except = false;
        void UpdatePrompts() { prompts[0] = menu_moveset; }
    private:
        // Um array para guardar todos os nossos prompts
        std::array<SkyPromptAPI::Prompt, 1> prompts = {menu_moveset};
    };

    class MovesetChangesSink final : public SkyPromptAPI::PromptSink,
                                     public clib_util::singleton::ISingleton<MovesetChangesSink> {
    public:
        // Retorna a lista de prompts que queremos monitorar
        std::span<const SkyPromptAPI::Prompt> GetPrompts() const override;

        // Função chamada quando um evento (ex: pressionar tecla) ocorre
        void ProcessEvent(SkyPromptAPI::PromptEvent event) const override;
        mutable bool except = false;
        void UpdatePrompts() {
            prompts[0] = moveset_actual;
            prompts[1] = moveset_next;
            prompts[2] = moveset_back;
        }
    private:
        // Um array para guardar todos os nossos prompts
        std::array<SkyPromptAPI::Prompt, 3> prompts = {moveset_actual,moveset_next, moveset_back};
    };

    // <-- NOVO: Classe para ouvir eventos de sacar/guardar arma -->
    class ActionEventHandler : public RE::BSTEventSink<SKSE::ActionEvent> {
    public:
        // Singleton para garantir uma única instância
        static ActionEventHandler* GetSingleton() {
            static ActionEventHandler singleton;
            return &singleton;
        }
        // Função que processa os eventos de ação do jogo
        RE::BSEventNotifyControl ProcessEvent(const SKSE::ActionEvent* a_event,
                                              RE::BSTEventSource<SKSE::ActionEvent>*) override;
    };


    class CameraChange : public RE::BSTEventSink<SKSE::CameraEvent> {
        
    public:
        static CameraChange* GetSingleton() {
            static CameraChange singleton;
            return &singleton;
        }
        RE::BSEventNotifyControl ProcessEvent(const SKSE::CameraEvent* a_event,
                                              RE::BSTEventSource<SKSE::CameraEvent>*) override;
    };

    class MenuOpen : public RE::BSTEventSink<RE::MenuOpenCloseEvent> {
    public:
        static MenuOpen* GetSingleton() {
            static MenuOpen singleton;
            return &singleton;
        }
        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                              RE::BSTEventSource<RE::MenuOpenCloseEvent>*);
    };

    class UpdateHandler : public RE::BSTEventSink<SKSE::NiNodeUpdateEvent> {
    public:
        static UpdateHandler* GetSingleton() {
            static UpdateHandler singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const SKSE::NiNodeUpdateEvent* a_event,
                                              RE::BSTEventSource<SKSE::NiNodeUpdateEvent>*) override;
    };
    struct ComboState {
        bool isTimerRunning = false;
        std::chrono::steady_clock::time_point comboTimeoutTimestamp;
        int lastMoveset = 0;
        int previousMoveset = 0;
    };
    inline ComboState g_comboState;  // Instância global única
    inline std::map<RE::FormID, ComboState> g_npcComboStates;
    inline std::mutex g_comboStateMutex;  // Mutex para proteger o acesso ao mapa
    inline constexpr float fComboTimeout = 1.0f;

    // Novo Event Sink para os eventos de animação do Papyrus
    class AnimationEventHandler : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static AnimationEventHandler* GetSingleton() {
            static AnimationEventHandler singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;

    };

    class NpcCycleSink : public RE::BSTEventSink<RE::BSAnimationGraphEvent> {
    public:
        static NpcCycleSink* GetSingleton() {
            static NpcCycleSink singleton;
            return &singleton;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                              RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override;
    };

    class NpcCombatTracker : public RE::BSTEventSink<RE::TESCombatEvent> {
    public:
        static NpcCombatTracker* GetSingleton() {
            static NpcCombatTracker singleton;
            return &singleton;
        }

        // Função chamada quando um evento de combate ocorre
        RE::BSEventNotifyControl ProcessEvent(const RE::TESCombatEvent* a_event,
                                              RE::BSTEventSource<RE::TESCombatEvent>*) override;

        static void RegisterSink(RE::Actor* a_actor);
        static void UnregisterSink(RE::Actor* a_actor);

        static void RegisterSinksForExistingCombatants();

    private:
        // Instância compartilhada do nosso processador de lógica
        inline static NpcCycleSink g_npcSink;

        // Guarda os FormIDs dos NPCs que já estamos ouvindo
        inline static std::set<RE::FormID> g_trackedNPCs;
        inline static std::shared_mutex g_mutex;
    };

    // Função que será chamada para gerar o número
    void TriggerSmartRandomNumber(const std::string& eventSource);

    inline std::array blockedMenus = {
        RE::DialogueMenu::MENU_NAME,    RE::JournalMenu::MENU_NAME,    RE::MapMenu::MENU_NAME,
        RE::StatsMenu::MENU_NAME,       RE::ContainerMenu::MENU_NAME,  RE::InventoryMenu::MENU_NAME,
        RE::TweenMenu::MENU_NAME,       RE::TrainingMenu::MENU_NAME,   RE::TutorialMenu::MENU_NAME,
        RE::LockpickingMenu::MENU_NAME, RE::SleepWaitMenu::MENU_NAME,  RE::LevelUpMenu::MENU_NAME,
        RE::Console::MENU_NAME,         RE::BookMenu::MENU_NAME,       RE::CreditsMenu::MENU_NAME,
        RE::LoadingMenu::MENU_NAME,     RE::MessageBoxMenu::MENU_NAME, RE::MainMenu::MENU_NAME,
        RE::RaceSexMenu::MENU_NAME,
    };

    inline bool IsAnyMenuOpen();
    inline bool IsThirdPerson();

    void NPCrandomNumber(RE::Actor* targetActor, const std::string& eventSource);

    void UpdateSkyPromptTexts();


}