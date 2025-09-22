#pragma once
#include <map>
#include <optional>
#include <string>
#include "Settings.h"  // Inclui as novas definições
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "ClibUtil/singleton.hpp"

struct FileSaveConfig;

struct NpcMovesetResult {
    int count = 0;
    int priority = 0;
};

// Enum para os tipos de regra
enum class RuleType { UniqueNPC, Faction, Keyword, Race, GeneralNPC, Player };

// Structs para guardar os dados carregados do jogo (para os pop-ups de seleção)
struct FactionInfo {
    RE::FormID formID;
    std::string editorID;
    std::string pluginName;
};

struct KeywordInfo {
    RE::FormID formID;
    std::string editorID;
    std::string pluginName;
};

struct RaceInfo {
    RE::FormID formID;
    std::string editorID;
    std::string fullName;
    std::string pluginName;
};

struct MovesetRule {
    RuleType type;
    std::string displayName;  // Nome amigável para a UI (ex: "Ulfric Stormcloak", "BanditFaction")
    std::string identifier;   // O identificador único (FormID em string ou EditorID)
    std::string pluginName;   // Relevante para FormIDs
    RE::FormID formID;
    // Cada regra tem seu próprio conjunto de categorias de armas.
    // Reutiliza a mesma estrutura que você já tem para o jogador e NPCs.
    std::map<std::string, WeaponCategory> categories;
};

RuleType RuleTypeFromString(const std::string& s);
std::string RuleTypeToString(RuleType type);

class AnimationManager : public clib_util::singleton::ISingleton<AnimationManager> {
public:
    void ScanAnimationMods();
    void DrawMainMenu();
    void DrawUserMovesetCreator();
    void DrawNPCMenu();
    static int GetMaxMovesetsFor(const std::string& category, int stanceIndex);
    int GetMaxMovesetsForNPC(RE::Actor* targetActor, const std::string& category, int stanceIndex);
    const std::map<std::string, WeaponCategory>& GetCategories() const;
    std::string GetStanceName(const std::string& categoryName, int stanceIndex);

    std::string GetCurrentMovesetName(const std::string& categoryName, int stanceIndex, int movesetIndex,
                                      int directionalState);
    bool _showRestartPopup = false; 
    void ScanDarAnimations();
    void LoadGameDataForNpcRules();
    void PopulateNpcList();
    NpcMovesetResult FindBestMovesetConfiguration(RE::Actor* actor, const std::string& categoryName);

private:
    std::map<std::string, WeaponCategory> _categories;
    std::map<std::string, WeaponCategory> _npcCategories;
    std::vector<AnimationModDef> _allMods;
    std::vector<SubAnimationDef> _darSubMovesets;
    bool _isAddDarModalOpen = false;
    // Armazena os caminhos de todos os config.json que nosso manager já tocou.
    std::set<std::filesystem::path> _managedFiles; 
    bool _preserveConditions = false;
    bool _isAddModModalOpen = false;
    CategoryInstance* _instanceToAddTo = nullptr;
    ModInstance* _modInstanceToAddTo = nullptr;
    // NOVO: Variáveis para o modal de criação de moveset
    ModInstance* _modInstanceToSaveAsCustom = nullptr;
    char _newMovesetNameBuffer[128] = "";

    void ProcessTopLevelMod(const std::filesystem::path& modPath);
    void DrawAddModModal();
    void SaveAllSettings();
    void UpdateOrCreateJson(const std::filesystem::path& jsonPath, const std::vector<FileSaveConfig>& configs);
    void AddCompareValuesCondition(rapidjson::Value& conditionsArray, const std::string& graphVarName, int value,
                                   rapidjson::Document::AllocatorType& allocator);
    // NOVA FUNÇÃO HELPER: Para adicionar condições booleanas (checkboxes)
    void AddCompareBoolCondition(rapidjson::Value& conditionsArray, const std::string& graphVarName, bool value,
                                 rapidjson::Document::AllocatorType& allocator);

    // Função do random aqui
    void AddRandomCondition(rapidjson::Value& conditionsArray, int value,
                                              rapidjson::Document::AllocatorType& allocator);
    // Para colocar direcionais no pai
    void AddNegatedCompareValuesCondition(rapidjson::Value& conditionsArray, const std::string& graphVarName, int value,
                                          rapidjson::Document::AllocatorType& allocator);
    void AddOcfWeaponExclusionConditions(rapidjson::Value& parentArray, rapidjson::Document::AllocatorType& allocator);

    void AddKeywordCondition(rapidjson::Value& parentArray, const std::string& editorID, bool isLeftHand, bool negated,
                             rapidjson::Document::AllocatorType& allocator);
    void AddCompetingKeywordExclusions(rapidjson::Value& parentArray, const WeaponCategory* currentCategory,
                                       bool isLeftHand, rapidjson::Document::AllocatorType& allocator);
    void AddKeywordOrConditions(rapidjson::Value& parentArray, const std::vector<std::string>& keywords,
                                bool isLeftHand, rapidjson::Document::AllocatorType& allocator);



    void SaveStanceNames();

    void LoadStanceNames();

    void DrawStanceEditorPopup();

    void DrawRestartPopup();

    void SaveUserMoveset();

    

    void DrawAddDarModal();

    void SaveCustomCategories();

    void LoadCustomCategories();

    void DrawCreateCategoryModal();

    void DrawCategoryManager();

    void AddCompareEquippedTypeCondition(rapidjson::Value& conditionsArray, double type, bool isLeftHand,
                                         rapidjson::Document::AllocatorType& allocator);

    void AddShieldCategoryExclusions(rapidjson::Value& parentArray, rapidjson::Document::AllocatorType& allocator);

    // --- NOVAS VARIÁVEIS PARA GERENCIAR MOVESETS DO USUÁRIO ---

    // Estrutura para manter um moveset de usuário em memória
    struct UserMoveset {
        std::string name;
        std::vector<SubAnimationInstance> subAnimations;
    };

    // Vetor com todos os movesets criados pelo usuário
    std::vector<UserMoveset> _userMovesets;

    // Estado da UI de criação/edição
    bool _isEditingUserMoveset = false;  // true quando estamos na tela de edição
    int _editingMovesetIndex = -1;       // Índice do moveset sendo editado, -1 para um novo
    UserMoveset _workspaceMoveset;       // "Mesa de trabalho" para criar/editar um moveset

    // Ponteiro para saber onde adicionar um sub-moveset vindo do modal
    UserMoveset* _userMovesetToAddTo = nullptr;



    // --- NOVAS FUNÇÕES PRIVADAS ---
    void DrawAnimationManager();  // Movido para private pois é chamado por DrawMainMenu
    void DrawCategoryUI(WeaponCategory& category);
    void DrawNPCCategoryUI(WeaponCategory& category);
    void DrawNPCManager();
    void SaveNPCSettings();

    void LoadUserMovesets();
    void SaveUserMovesets();
    void DrawUserMovesetManager();  // A UI principal para esta nova seção
    void DrawUserMovesetEditor();   // A UI para a tela de edição
    void RebuildUserMovesetLibrary();  // <-- ADICIONE ESTA LINHA

    // Filtro de pesquisa
    char _movesetFilter[128] = "";
    char _subMovesetFilter[128] = "";
    // Da load na ordem dos movesets e submovesets
    void LoadStateForSubAnimation(size_t modIdx, size_t subAnimIdx);

    // --- NOVAS FUNÇÕES DE CARREGAMENTO/SALVAMENTO DA UI ---

    void SaveCycleMovesets();
    void LoadCycleMovesets();


    // Função auxiliar para encontrar um mod pelo nome
    std::optional<size_t> FindModIndexByName(const std::string& name);
    // Função auxiliar para encontrar uma sub-animação pelo nome dentro de um mod
    std::optional<size_t> FindSubAnimIndexByName(size_t modIdx, const std::string& name);

     // NOVO CACHE ESTÁTICO: Armazena a contagem de movesets para acesso rápido.
    // A chave é o nome da categoria (ex: "Swords"), o valor é um array de 4 ints (um para cada stance).
    inline static std::map<std::string, std::array<int, 4>> _maxMovesetsPerCategory;
    inline static std::map<RE::FormID, std::map<std::string, std::array<int, 4>>> _maxMovesetsPerCategory_NPC;

    // NOVA FUNÇÃO PRIVADA: Usada internamente para preencher o cache.
    void UpdateMaxMovesetCache();

    bool _isEditStanceModalOpen = false;
    WeaponCategory* _categoryToEdit = nullptr;
    int _stanceIndexToEdit = -1;
    char _editStanceNameBuffer[64] = "";

     // Buffers para os campos de texto da UI
    char _newMovesetName[128] = "";
    char _newMovesetAuthor[128] = "";
    char _newMovesetDesc[256] = "";

    // Estado da seleção
    int _newMovesetCategoryIndex = 0;  // Índice para o combo de categorias
    bool _newMovesetIsBFCO = false;

    struct CreatorSubAnimationInstance {
        const SubAnimationDef* sourceDef;  // Ponteiro para a definição original
        std::array<char, 128> editedName;  // Nome editável
        bool isBFCO = false;
        // Flags para todas as checkboxes
        bool pFront = false, pBack = false, pLeft = false, pRight = false;
        bool pFrontRight = false, pFrontLeft = false, pBackRight = false, pBackLeft = false;
        bool pRandom = false, pDodge = false;
        std::map<std::string, bool> hkxFileSelection;
    };


    struct CreatorStance {
        std::vector<CreatorSubAnimationInstance> subMovesets;
    };
    std::map<std::string, std::array<CreatorStance, 4>> _movesetCreatorStances;

    std::array<bool, 4> _newMovesetStanceEnabled = {true, true, true, true};  // Para habilitar/desabilitar stances

    // Ponteiros para gerenciar o modal de adição
    CreatorStance* _stanceToAddTo = nullptr;
    char _categoryFilterBuffer[128] = "";

    // ---> INÍCIO DAS ADIÇÕES: Estado da UI do Criador de Categoria <---
    bool _isCreateCategoryModalOpen = false;
    char _newCategoryNameBuffer[128] = "";
    int _newCategoryBaseIndex = 0;
    char _newCategoryKeywordsBuffer[256] = "";
    bool _newCategoryIsDual = false;
    bool _newCategoryIsShield = false;
    int _newCategoryLeftHandBaseIndex = 0;
    char _newCategoryLeftHandKeywordsBuffer[256] = "";
    // ---> FIM DAS ADIÇÕES <---

    // ---> INÍCIO DAS ADIÇÕES: Estado da UI do Editor de Categoria <---
    WeaponCategory* _categoryToEditPtr = nullptr;
    char _originalCategoryName[128] = "";  // Para saber qual arquivo renomear/deletar
    // ---> FIM DAS ADIÇÕES <---

    std::map<std::string, bool> _newMovesetCategorySelection;

    struct NPCInfo {
        RE::FormID formID;
        std::string editorID;
        std::string name;
        std::string pluginName;
    };

    // --- Variáveis para o novo Modal de Seleção de NPC ---

    // Lista completa de todos os NPCs encontrados no jogo
    std::vector<NPCInfo> _fullNpcList;
    // Lista de plugins (.esp) únicos para popular o filtro
    std::vector<std::string> _pluginList;
    // Flag para garantir que o escaneamento pesado só rode uma vez
    bool _npcListPopulated = false;
    // Flag para controlar a visibilidade do modal
    bool _isNpcSelectionModalOpen = false;
    // Buffer para o texto do filtro de pesquisa de NPC
    char _npcFilterBuffer[128] = "";
    // Índice do plugin selecionado no filtro
    int _selectedPluginIndex = 0;

    // Onde as configurações de NPCs específicos serão armazenadas (FormID -> Configurações)
    // A configuração é um mapa de Categoria -> Dados da Categoria, espelhando _categories
    struct SpecificNpcConfig {
        std::string name;
        std::string pluginName;
        std::map<std::string, WeaponCategory> categories;
    };

    // Substitua a declaração antiga de _specificNpcConfigs por esta:
    std::map<RE::FormID, SpecificNpcConfig> _specificNpcConfigs;

    // --- Novas Funções Privadas ---
    
    void DrawNpcSelectionModal();

    void PopulateHkxFiles(CreatorSubAnimationInstance& instance);

    RE::FormID _currentlySelectedNpcFormID = 0;
    std::vector<const char*> _npcSelectorList;

     // --- ADICIONE AS NOVAS ESTRUTURAS ---
    std::vector<MovesetRule> _npcRules;  // A lista principal com todas as regras criadas

    // Listas para popular os menus de seleção
    std::vector<FactionInfo> _allFactions;
    std::vector<KeywordInfo> _allKeywords;
    std::vector<RaceInfo> _allRaces;
    MovesetRule _generalNpcRule; 
    // --- ADICIONE ESTAS NOVAS VARIÁVEIS PARA A UI DE REGRAS ---
    int _ruleFilterType = 0;  // 0=Todos, 1=NPC, 2=Keyword, 3=Facção, 4=Raça
    char _ruleFilterText[128] = "";

    // Para controlar qual regra está sendo editada
    MovesetRule* _ruleToEdit = nullptr;
    ModInstance* _instanceBeingEdited = nullptr;
    // Para controlar o pop-up de criação de regras
    bool _isCreateRuleModalOpen = false;
    RuleType _ruleTypeToCreate;
    // --- FIM DA ADIÇÃO ---
    void AddIsActorBaseCondition(rapidjson::Value& conditionsArray, const std::string& plugin, RE::FormID formID,
                                 bool negated, rapidjson::Document::AllocatorType& allocator);
    void AddIsInFactionCondition(rapidjson::Value& conditionsArray, const std::string& plugin, RE::FormID formID,
                                 rapidjson::Document::AllocatorType& allocator);
    void AddHasKeywordCondition(rapidjson::Value& conditionsArray, const std::string& plugin, RE::FormID formID,
                                rapidjson::Document::AllocatorType& allocator);
    void AddIsRaceCondition(rapidjson::Value& conditionsArray, const std::string& plugin, RE::FormID formID,
                            rapidjson::Document::AllocatorType& allocator);
    int GetPriorityForType(RuleType type);
    
};

struct FileSaveConfig {
    int instance_index;
    int order_in_playlist;
    const WeaponCategory* category;
    // Campos adicionados para carregar o estado das checkboxes
    bool isParent = false;
    std::set<int> childDirections;
    bool isNPC = false;
    RE::FormID npcFormID = 0;
    RuleType ruleType;
    RE::FormID formID;
    std::string pluginName;
    std::string ruleIdentifier;
    bool pFront = false;
    bool pBack = false;
    bool pLeft = false;
    bool pRight = false;
    bool pFrontRight = false;
    bool pFrontLeft = false;
    bool pBackRight = false;
    bool pBackLeft = false;
    bool pRandom = false;
    bool pDodge = false;
};

