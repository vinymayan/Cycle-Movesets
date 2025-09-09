#pragma once
#include <map>
#include <optional>
#include <string>
#include "Settings.h"  // Inclui as novas definições
#include "rapidjson/document.h"
#include "rapidjson/prettywriter.h"
#include "ClibUtil/singleton.hpp"

struct FileSaveConfig;

class AnimationManager : public clib_util::singleton::ISingleton<AnimationManager> {
public:
    void ScanAnimationMods();
    void DrawMainMenu();
    void DrawUserMovesetCreator();
    void DrawNPCMenu();
    static int GetMaxMovesetsFor(const std::string& category, int stanceIndex);
    static int GetMaxMovesetsForNPC(const std::string& category, int stanceIndex);
    const std::map<std::string, WeaponCategory>& GetCategories() const;
    std::string GetStanceName(const std::string& categoryName, int stanceIndex);

    std::string GetCurrentMovesetName(const std::string& categoryName, int stanceIndex, int movesetIndex,
                                      int directionalState);
    bool _showRestartPopup = false; 
    void ScanDarAnimations();

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
    inline static std::map<std::string, std::array<int, 4>> _maxMovesetsPerCategory_NPC;

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

        // Flags para todas as checkboxes
        bool pFront = false, pBack = false, pLeft = false, pRight = false;
        bool pFrontRight = false, pFrontLeft = false, pBackRight = false, pBackLeft = false;
        bool pRandom = false, pDodge = false;
    };
    struct StanceContent {
        std::vector<CreatorSubAnimationInstance> subMovesets;
    };
    std::array<StanceContent, 4> _newMovesetStances;
    std::array<bool, 4> _newMovesetStanceEnabled = {true, true, true, true};  // Para habilitar/desabilitar stances

    // Ponteiros para gerenciar o modal de adição
    StanceContent* _stanceToAddTo = nullptr;
};

struct FileSaveConfig {
    int instance_index;
    int order_in_playlist;
    const WeaponCategory* category;
    // Campos adicionados para carregar o estado das checkboxes
    bool isParent = false;
    bool isNPC = false;

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
