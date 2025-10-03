#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "MCP.h"

struct DPATags {
    bool hasA = false;  // Para BFCO_PowerAttackA.hkx
    bool hasB = false;  // Para BFCO_PowerAttackB.hkx
    bool hasL = false;  // Para BFCO_PowerAttackL.hkx
    bool hasR = false;  // Para BFCO_PowerAttackR.hkx

    // Fun��o auxiliar para verificar se algum DPA est� dispon�vel
    bool any() const { return hasA || hasB || hasL || hasR; }
};
struct MovesetTags {
    DPATags dpaTags;
    bool hasCPA = false;
};
// --- Defini��es da Biblioteca ---
struct SubAnimationDef {
    std::string name;
    std::filesystem::path path;
    int attackCount = 0;       // Contagem de arquivos BFCO_Attack
    int powerAttackCount = 0;  // Contagem de arquivos BFCO_PowerAttack
    bool hasIdle = false;      // Presen�a de arquivos "idle"
    bool hasAnimations = false;
    DPATags dpaTags;
    bool hasCPA = false;  
};
struct AnimationModDef {
    std::string name;
    std::string author;
    std::vector<SubAnimationDef> subAnimations;
};

// --- Estruturas de Configura��o do Usu�rio ---
struct SubAnimationInstance {
    // --- ALTERADO: Usamos nomes para salvar/carregar. Os �ndices ser�o preenchidos em tempo de execu��o. ---
    std::string sourceModName;  // Nome do mod de origem (e.g., "BFCO")
    std::string sourceSubName;  // Nome da sub-anima��o de origem (e.g., "700036")
    size_t sourceModIndex;
    size_t sourceSubAnimIndex;
    std::array<char, 128> editedName{};
    bool isSelected = true;
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
    DPATags dpaTags;
    bool hasCPA = false;
};

struct ModInstance {
    size_t sourceModIndex;
    bool isSelected = true;
    std::vector<SubAnimationInstance> subAnimationInstances;
    int level = 0;  // Condi��o de N�vel M�nimo
    int hp = 100;   // Condi��o de HP M�ximo (em porcentagem)
    int st = 100;
    int mn = 100;
    int order = 0;
};

struct CategoryInstance {
    std::vector<ModInstance> modInstances;
};

struct WeaponCategory {
    std::string name;
    double equippedTypeValue;
    double leftHandEquippedTypeValue = -1.0;
    int activeInstanceIndex = 0;
    bool isDualWield = false;
    bool isShieldCategory = false;
    std::vector<std::string> keywords;
    std::vector<std::string> leftHandKeywords;
    std::array<CategoryInstance, 4> instances;
    // --- NOVO ---
    // Armazena os nomes customizados das stances
    std::array<std::string, 4> stanceNames;
    // Buffer para edi��o no ImGui (evita problemas com std::string)
    std::array<std::array<char, 64>, 4> stanceNameBuffers;

    bool isCustom = false;
    std::string baseCategoryName;
};

struct UserMoveset {
    std::string name;
    std::vector<SubAnimationInstance> subAnimations;
};

void WheelerKeys();
inline int WheelerKeyboard = 0;
inline int WheelerGamepad = 0;
