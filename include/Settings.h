#pragma once
#include <array>
#include <filesystem>
#include <string>
#include <vector>
#include "MCP.h"

// --- Definições da Biblioteca ---
struct SubAnimationDef {
    std::string name;
    std::filesystem::path path;
    int attackCount = 0;       // Contagem de arquivos BFCO_Attack
    int powerAttackCount = 0;  // Contagem de arquivos BFCO_PowerAttack
    bool hasIdle = false;      // Presença de arquivos "idle"
    bool hasAnimations = false;
};
struct AnimationModDef {
    std::string name;
    std::string author;
    std::vector<SubAnimationDef> subAnimations;
};

// --- Estruturas de Configuração do Usuário ---
struct SubAnimationInstance {
    // --- ALTERADO: Usamos nomes para salvar/carregar. Os índices serão preenchidos em tempo de execução. ---
    std::string sourceModName;  // Nome do mod de origem (e.g., "BFCO")
    std::string sourceSubName;  // Nome da sub-animação de origem (e.g., "700036")
    size_t sourceModIndex;
    size_t sourceSubAnimIndex;
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
};

struct ModInstance {
    size_t sourceModIndex;
    bool isSelected = true;
    std::vector<SubAnimationInstance> subAnimationInstances;
};

struct CategoryInstance {
    std::vector<ModInstance> modInstances;
};

struct WeaponCategory {
    std::string name;
    double equippedTypeValue;
    int activeInstanceIndex = 0;
    bool isDualWield = false;
    std::vector<std::string> keywords;
    std::array<CategoryInstance, 4> instances;
    // --- NOVO ---
    // Armazena os nomes customizados das stances
    std::array<std::string, 4> stanceNames;
    // Buffer para edição no ImGui (evita problemas com std::string)
    std::array<std::array<char, 64>, 4> stanceNameBuffers;
};

struct UserMoveset {
    std::string name;
    std::vector<SubAnimationInstance> subAnimations;
};