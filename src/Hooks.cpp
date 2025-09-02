#include <algorithm>
#include <format>
#include <fstream>
#include <string>
#include "Events.h"
#include "SKSEMCP/SKSEMenuFramework.hpp"
#include "rapidjson/document.h"
#include "rapidjson/filereadstream.h"
#include "rapidjson/filewritestream.h"
#include "rapidjson/prettywriter.h"
#include "MCP.h"
#include "OARAPI.h"


// Função auxiliar para copiar um único arquivo com logs
void CopySingleFile(const std::filesystem::path& sourceFile, const std::filesystem::path& destinationPath,
                    int& filesCopied) {
    try {
        std::filesystem::copy_file(sourceFile, destinationPath / sourceFile.filename(),
                                   std::filesystem::copy_options::overwrite_existing);
        filesCopied++;
    } catch (const std::filesystem::filesystem_error& e) {
        SKSE::log::error("Falha ao copiar arquivo: {}. Erro: {}", sourceFile.string(), e.what());
    }
}

void ProcessCycleDarFile(const std::filesystem::path& cycleDarJsonPath) {
    SKSE::log::info("Processando CycleDar.json em: {}", cycleDarJsonPath.string());

    // 1. Abre e lê o arquivo JSON
    std::ifstream fileStream(cycleDarJsonPath);
    if (!fileStream) {
        SKSE::log::error("Falha ao abrir {}", cycleDarJsonPath.string());
        return;
    }
    std::string jsonContent((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
    fileStream.close();

    // 2. Faz o parse do JSON
    rapidjson::Document doc;
    doc.Parse(jsonContent.c_str());

    if (doc.HasParseError()) {
        SKSE::log::error("Erro no parse do JSON em {}", cycleDarJsonPath.string());
        return;
    }

    // 3. Verifica se a conversão já foi feita
    if (doc.HasMember("conversionDone") && doc["conversionDone"].IsBool() && doc["conversionDone"].GetBool()) {
        SKSE::log::info("A cópia para {} já foi concluída anteriormente. Pulando.", cycleDarJsonPath.string());
        return;
    }

    if (!doc.IsObject() || !doc.HasMember("pathDar") || !doc["pathDar"].IsString()) {
        SKSE::log::error("'pathDar' não encontrado ou inválido em {}", cycleDarJsonPath.string());
        return;
    }

    // 4. Constrói os caminhos
    std::string relativePath = doc["pathDar"].GetString();
    std::filesystem::path sourcePath = "Data" / std::filesystem::path(relativePath);
    std::filesystem::path destinationPath = cycleDarJsonPath.parent_path();

    if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
        SKSE::log::warn("Pasta de origem não existe: {}", sourcePath.string());
        return;
    }

    SKSE::log::info("Copiando arquivos de '{}' para '{}'", sourcePath.string(), destinationPath.string());
    int filesCopied = 0;

    // 5. NOVA LÓGICA: VERIFICA A LISTA DE ARQUIVOS
    bool copyAll = true;  // Flag para determinar o modo de cópia
    if (doc.HasMember("filesToCopy") && doc["filesToCopy"].IsArray() && !doc["filesToCopy"].Empty()) {
        copyAll = false;
    }

    if (copyAll) {
        SKSE::log::info("Modo: Copiando todos os arquivos .hkx da pasta.");
        // Itera e copia todos os arquivos .hkx (comportamento original)
        for (const auto& fileEntry : std::filesystem::directory_iterator(sourcePath)) {
            if (fileEntry.is_regular_file()) {
                std::string extension = fileEntry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                if (extension == ".hkx") {
                    CopySingleFile(fileEntry.path(), destinationPath, filesCopied);
                }
            }
        }
    } else {
        SKSE::log::info("Modo: Copiando arquivos especificados na lista 'filesToCopy'.");
        const rapidjson::Value& filesArray = doc["filesToCopy"];
        for (rapidjson::SizeType i = 0; i < filesArray.Size(); i++) {
            if (filesArray[i].IsString()) {
                std::string filename = filesArray[i].GetString();
                std::filesystem::path sourceFile = sourcePath / filename;

                if (std::filesystem::exists(sourceFile)) {
                    CopySingleFile(sourceFile, destinationPath, filesCopied);
                } else {
                    SKSE::log::warn("Arquivo especificado não encontrado na origem: {}", sourceFile.string());
                }
            }
        }
    }

    SKSE::log::info("Cópia concluída. {} arquivos movidos.", filesCopied);

    // 6. Atualiza o JSON e salva no arquivo
    if (doc.HasMember("conversionDone")) {
        doc["conversionDone"].SetBool(true);
    } else {
        doc.AddMember("conversionDone", true, doc.GetAllocator());
    }

    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);

    std::ofstream outFile(cycleDarJsonPath);
    if (!outFile) {
        SKSE::log::error("Falha ao abrir {} para escrita!", cycleDarJsonPath.string());
        return;
    }

    outFile << buffer.GetString();
    outFile.close();

    SKSE::log::info("Arquivo JSON {} atualizado com sucesso.", cycleDarJsonPath.string());
}

// --- DESAFIO 1: Nova função para escanear a pasta do sub-moveset em busca de tags ---
void ScanSubAnimationFolderForTags(const std::filesystem::path& subAnimPath,
                                                     SubAnimationDef& subAnimDef) {
    if (!std::filesystem::exists(subAnimPath) || !std::filesystem::is_directory(subAnimPath)) {
        return;
    }

    // --- PONTO 2: Processar CycleDar.json ANTES de escanear as tags ---
    // Procura pelo arquivo CycleDar.json e copia os arquivos .hkx se ele existir.
    // Isso garante que os arquivos copiados estarão presentes para o escaneamento de tags abaixo.
    std::filesystem::path cycleDarPath = subAnimPath / "CycleDar.json";
    if (std::filesystem::exists(cycleDarPath) && std::filesystem::is_regular_file(cycleDarPath)) {
        ProcessCycleDarFile(cycleDarPath);  // Chama a nova função helper
    }


    subAnimDef.attackCount = 0;
    subAnimDef.powerAttackCount = 0;
    subAnimDef.hasIdle = false;

   // Itera sobre todos os arquivos na pasta para encontrar tags de animação
    for (const auto& fileEntry : std::filesystem::directory_iterator(subAnimPath)) {
        if (fileEntry.is_regular_file()) {
            // --- PONTO 3: Ler apenas arquivos .hkx ---
            std::string extension = fileEntry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            // A lógica de verificação de tags agora só é executada para arquivos .hkx
            if (extension == ".hkx") {
                std::string filename = fileEntry.path().filename().string();
                std::string lowerFilename = filename;
                std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(), ::tolower);

                if (filename.rfind("BFCO_Attack", 0) == 0) {  // Prefixo "BFCO_Attack"
                    subAnimDef.attackCount++;
                }
                if (filename.rfind("BFCO_PowerAttack", 0) == 0) {  // Prefixo "BFCO_PowerAttack"
                    subAnimDef.powerAttackCount++;
                }
                if (lowerFilename.find("idle") != std::string::npos) {  // Contém "idle"
                    subAnimDef.hasIdle = true;
                }
            }
        }
    }
}

// --- Lógica de Escaneamento (Carrega a Biblioteca) ---
void AnimationManager::ScanAnimationMods() {
    SKSE::log::info("Iniciando escaneamento da biblioteca de animações...");
    _categories.clear();
    _allMods.clear();

    const std::filesystem::path oarRootPath = "Data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer";
    // ESTRUTURA MELHORADA: Facilita a definição de categorias e suas propriedades
    struct CategoryDefinition {
        std::string name;
        double typeValue;
        bool isDual;
        std::vector<std::string> keywords;
    };

    std::vector<CategoryDefinition> categoryDefinitions = {
        {"Swords", 1.0, false, {}},
        {"Daggers", 2.0, false, {}},
        {"War Axes", 3.0, false, {}},
        {"Maces", 4.0, false, {}}, 
        {"Greatswords", 5.0, false, {}},
        //{"Bows", 7.0, false, {}},
        {"Battleaxes", 6.0, false, {}},
        {"Warhammers", 10.0, false, {}},

                                                          
        /*{"Alteration Spell", 12.0, false, {"MagicAlteration"}},
        {"Illusion Spell", 13.0, false, {"MagicIllusion"}},
        {"Destruction Spell", 14.0, false, {"MagicDestruction"}},
        {"Magic", 14.0, false, {}},
        {"Conjuration Spell", 15.0, false, {"MagicConjuration"}},
        {"Restoration Spell", 16.0, false, {"MagicRestoration"}},*/
        {"Katanas", 1.0, false, {"OCF_WeapTypeKatana1H","WeapTypeKatana"}},
        {"Claws", 2.0, false, {"OCF_WeapTypeClaws1H","WeapTypeClaw"}},
        {"Pike", 5.0, false, {"OCF_WeapTypePike2H", "WeapTypePike"}},
        {"Twinblades", 5.0, false, {"OCF_WeapTypeTwinblade2H", "WeapTypePike"}},
        {"Halberd", 6.0, false, {"OCF_WeapTypeHalberd2H", "WeapTypeHalberd"}},
        {"Quarterstaff", 10.0, false, {"OCF_WeapTypeHalberd2H", "WeapTypeQtrStaff"}},
        {"Rapier", 1.0, false, {"OCF_WeapTypeRapier1H", "WeapTypeRapier"}},
        {"Whip", 4.0, false, {"OCF_WeapTypeWhip1H", "WeapTypeWhip"}},
                                                           

        // CATEGORIAS DUAL WIELD
        
        /*{"Alteration Spells", 12.0, true, {"MagicAlteration"}},
        {"Illusion Spells", 13.0, true, {"MagicIllusion"}},
        {"Destruction Spells", 14.0, true, {"MagicDestruction"}},
        {"Conjuration Spells", 15.0, true, {"MagicConjuration"}},
        {"Restoration Spells", 16.0, true, {"MagicRestoration"}},*/
        {"Dual Swords", 1.0, true, {}},
        {"Dual Daggers", 2.0, true, {}},
        {"Dual War Axes", 3.0, true, {}},
        {"Dual Maces", 4.0, true, {}},
        {"Dual Katanas", 1.0, true, {"OCF_WeapTypeKatana1H", "WeapTypeKatana"}},
        {"Dual Rapier", 1.0, true, {"OCF_WeapTypeRapier1H", "WeapTypeRapier"}},
        {"Dual Claws", 2.0, true, {"OCF_WeapTypeClaws1H", "WeapTypeClaw"}},

        {"Unarmed", 0.0, true, {}}};


    for (const auto& def : categoryDefinitions) {
        _categories[def.name].name = def.name;
        _categories[def.name].equippedTypeValue = def.typeValue;
        _categories[def.name].isDualWield = def.isDual;
        _categories[def.name].keywords = def.keywords;

        // --- NOVO: Inicializa os nomes e buffers das stances ---
        for (int i = 0; i < 4; ++i) {
            std::string defaultName = std::format("Stance {}", i + 1);
            _categories[def.name].stanceNames[i] = defaultName;
            strcpy_s(_categories[def.name].stanceNameBuffers[i].data(),
                     _categories[def.name].stanceNameBuffers[i].size(), defaultName.c_str());
        }
    }
    LoadStanceNames();


    if (!std::filesystem::exists(oarRootPath)) return;
    for (const auto& entry : std::filesystem::directory_iterator(oarRootPath)) {
        if (entry.is_directory()) {
            ProcessTopLevelMod(entry.path());
        }
    }
    SKSE::log::info("Escaneamento de arquivos finalizado. {} mods carregados.", _allMods.size());

    // Agora que temos todos os mods, vamos encontrar quais arquivos já gerenciamos.
    SKSE::log::info("Verificando arquivos previamente gerenciados...");
    _managedFiles.clear();
    for (const auto& mod : _allMods) {
        for (const auto& subAnim : mod.subAnimations) {
            if (std::filesystem::exists(subAnim.path)) {
                std::ifstream fileStream(subAnim.path);
                std::string content((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
                fileStream.close();
                if (content.find("OAR_CYCLE_MANAGER_CONDITIONS") != std::string::npos) {
                    _managedFiles.insert(subAnim.path);
                }
            }
        }
    }
    SKSE::log::info("Encontrados {} arquivos gerenciados.", _managedFiles.size());

    // --- NOVA SEÇÃO: Carregar e integrar movesets do usuário ---
    LoadUserMovesets();

    for (const auto& userMoveset : _userMovesets) {
        AnimationModDef modDef;
        modDef.name = userMoveset.name;
        modDef.author = "Usuário";  // Autor padrão

        for (const auto& subInstance : userMoveset.subAnimations) {
            // Verifica se os índices são válidos para evitar crashes
            if (subInstance.sourceModIndex < _allMods.size()) {
                const auto& sourceMod = _allMods[subInstance.sourceModIndex];
                if (subInstance.sourceSubAnimIndex < sourceMod.subAnimations.size()) {
                    // Adiciona a definição da sub-animação original ao nosso novo mod virtual
                    modDef.subAnimations.push_back(sourceMod.subAnimations[subInstance.sourceSubAnimIndex]);
                }
            }
        }
        _allMods.push_back(modDef);
    }
    SKSE::log::info("Integração finalizada. Total de {} mods na biblioteca (incluindo de usuário).", _allMods.size());
    // -- -NOVA CHAMADA-- -
    // Agora que a biblioteca de mods (_allMods) está completa, carregamos a configuração da UI.
    _npcCategories = _categories;
    LoadCycleMovesets();
    // --- ADICIONADO: Inicializa as categorias dos NPCs como uma cópia das do player ---
    
    // TODO: No futuro, você pode querer carregar uma configuração separada para NPCs aqui.
    // Por enquanto, isso garante que a UI dos NPCs tenha as mesmas categorias de armas.
    SKSE::log::info("Categorias de armas para NPCs inicializadas.");
}

void AnimationManager::ProcessTopLevelMod(const std::filesystem::path& modPath) {
    std::filesystem::path configPath = modPath / "config.json";
    if (!std::filesystem::exists(configPath)) return;
    std::ifstream fileStream(configPath);
    std::string jsonContent((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
    fileStream.close();
    rapidjson::Document doc;
    doc.Parse(jsonContent.c_str());
    if (doc.IsObject() && doc.HasMember("name") && doc.HasMember("author")) {
        AnimationModDef modDef;
        modDef.name = doc["name"].GetString();
        modDef.author = doc["author"].GetString();
        for (const auto& subEntry : std::filesystem::recursive_directory_iterator(modPath)) {
            if (subEntry.is_directory() && std::filesystem::exists(subEntry.path() / "config.json")) {
                if (std::filesystem::equivalent(modPath, subEntry.path())) continue;
                SubAnimationDef subAnimDef;
                subAnimDef.name = subEntry.path().filename().string();
                subAnimDef.path = subEntry.path() / "config.json";
                // --- DESAFIO 1: Chamar a função de escaneamento de tags ---
                ScanSubAnimationFolderForTags(subEntry.path(), subAnimDef);
                modDef.subAnimations.push_back(subAnimDef);
            }
        }
        _allMods.push_back(modDef);
    }
}

// --- Lógica da Interface de Usuário ---
void AnimationManager::DrawAddModModal() {
    if (_isAddModModalOpen) {
        if (_instanceToAddTo) {
            ImGui::OpenPopup(LOC("add_moveset"));
        } else if (_modInstanceToAddTo || _userMovesetToAddTo) {
            ImGui::OpenPopup(LOC("add_animation"));
        }
        _isAddModModalOpen = false;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 modal_list_size = ImVec2(viewport->Size.x * 0.5f, viewport->Size.y * 0.5f);
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    // Modal LOC("add_moveset") (sem alterações, já estava correto)
    if (ImGui::BeginPopupModal(LOC("add_moveset"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Biblioteca de Movesets");
        ImGui::Separator();
        ImGui::InputText("Filter", _movesetFilter, 128);
        if (ImGui::BeginChild("BibliotecaMovesets", ImVec2(modal_list_size), true)) {
            std::string filter_str = _movesetFilter;
            std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);
            for (size_t modIdx = 0; modIdx < _allMods.size(); ++modIdx) {
                const auto& modDef = _allMods[modIdx];
                std::string mod_name_str = modDef.name;
                std::transform(mod_name_str.begin(), mod_name_str.end(), mod_name_str.begin(), ::tolower);
                if (filter_str.empty() || mod_name_str.find(filter_str) != std::string::npos) {
                    
                    if (ImGui::Button((LOC("add") + modDef.name).c_str())) {
                        ModInstance newModInstance;
                        newModInstance.sourceModIndex = modIdx;
                        for (size_t subIdx = 0; subIdx < modDef.subAnimations.size(); ++subIdx) {
                            SubAnimationInstance newSubInstance;
                            newSubInstance.sourceModIndex = modIdx;
                            newSubInstance.sourceSubAnimIndex = subIdx;
                            newModInstance.subAnimationInstances.push_back(newSubInstance);
                        }
                        _instanceToAddTo->modInstances.push_back(newModInstance);
                    }
                    ImGui::SameLine(240);
                    ImGui::Text("%s", modDef.name.c_str());
                    
                }
            }
        }
        ImGui::EndChild();
        if (ImGui::Button(LOC("close"))) {
            strcpy_s(_movesetFilter, "");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    // Modal LOC("add_animation") (COM AS CORREÇÕES)
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal(LOC("add_animation"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Biblioteca de Animações");
        ImGui::Separator();
        ImGui::InputText("Filter", _subMovesetFilter, 128);

        if (ImGui::BeginChild("BibliotecaSubMovesets", ImVec2(modal_list_size), true)) {
            std::string filter_str = _subMovesetFilter;
            std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

            for (size_t modIdx = 0; modIdx < _allMods.size(); ++modIdx) {
                const auto& modDef = _allMods[modIdx];
                std::string mod_name_str = modDef.name;
                std::transform(mod_name_str.begin(), mod_name_str.end(), mod_name_str.begin(), ::tolower);
                bool parent_matches = mod_name_str.find(filter_str) != std::string::npos;
                bool child_matches = false;
                if (!parent_matches) {
                    for (const auto& subAnim : modDef.subAnimations) {
                        std::string sub_name_str = subAnim.name;
                        std::transform(sub_name_str.begin(), sub_name_str.end(), sub_name_str.begin(), ::tolower);
                        if (sub_name_str.find(filter_str) != std::string::npos) {
                            child_matches = true;
                            break;
                        }
                    }
                }

                if (filter_str.empty() || parent_matches || child_matches) {
                    if (ImGui::TreeNode(modDef.name.c_str())) {
                        for (size_t subAnimIdx = 0; subAnimIdx < modDef.subAnimations.size(); ++subAnimIdx) {
                            const auto& subAnimDef = modDef.subAnimations[subAnimIdx];
                            std::string sub_name_str = subAnimDef.name;
                            std::transform(sub_name_str.begin(), sub_name_str.end(), sub_name_str.begin(), ::tolower);

                            if (filter_str.empty() || sub_name_str.find(filter_str) != std::string::npos) {
                                ImGui::PushID(static_cast<int>(
                                    modIdx * 1000 +
                                    subAnimIdx));  // <-- CORREÇÃO: PushID antes de qualquer item da linha.

                                

                                // <-- CORREÇÃO 1: Alinha o botão à direita com um espaçamento.
                                float button_width = 100.0f;

                                ImVec2 content_avail;
                                ImGui::GetContentRegionAvail(&content_avail);  // Pega a região disponível

                                if (ImGui::Button("Adicionar", ImVec2(button_width, 0))) {
                                    SubAnimationInstance newSubInstance;
                                    newSubInstance.sourceModIndex = modIdx;
                                    newSubInstance.sourceSubAnimIndex = subAnimIdx;
                                    const auto& sourceMod = _allMods[modIdx];
                                    const auto& sourceSubAnim = sourceMod.subAnimations[subAnimIdx];
                                    newSubInstance.sourceModName = sourceMod.name;
                                    newSubInstance.sourceSubName = sourceSubAnim.name;
                                    if (_modInstanceToAddTo) {
                                        _modInstanceToAddTo->subAnimationInstances.push_back(newSubInstance);
                                    } else if (_userMovesetToAddTo) {
                                        _userMovesetToAddTo->subAnimations.push_back(newSubInstance);
                                    }
                                }
                                // Se a largura disponível for maior que o botão, alinha
                                if (content_avail.x > button_width) {
                                    ImGui::SameLine(button_width +40);
                                } else {
                                    ImGui::SameLine();  // Fallback para evitar posições negativas
                                }

                                
                                ImGui::Text("%s", subAnimDef.name.c_str());
                                ImGui::PopID();  // <-- CORREÇÃO 2: PopID movido para DENTRO do loop, no final da
                                                 // iteração.
                            }
                        }
                        ImGui::TreePop();
                    }
                }
            }
        }
        ImGui::EndChild();
        if (ImGui::Button(LOC("close"))) {
            strcpy_s(_subMovesetFilter, "");  // Limpa o filtro ao fechar
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

// Esta é a nova função principal da UI que você registrará no SKSEMenuFramework
void AnimationManager::DrawMainMenu() {
    // Primeiro, desenhamos o sistema de abas
    if (ImGui::BeginTabBar("MainTabs")) {
        if (ImGui::BeginTabItem(LOC("tab_movesets"))) {
            DrawAnimationManager();  // Chama a UI da primeira aba
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(LOC("tab_user_movesets"))) {
            DrawUserMovesetManager();  // Chama a UI da segunda aba
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // CORREÇÃO: Chamamos a função do modal aqui, fora de qualquer aba.
    // Ele só será desenhado quando a flag _isAddModModalOpen for verdadeira,
    // mas agora ele não pertence a nenhuma aba específica.
    DrawAddModModal();
    DrawStanceEditorPopup();
    DrawRestartPopup();
}

void AnimationManager::DrawNPCMenu() { 
    DrawNPCManager();
    DrawAddModModal();
    DrawRestartPopup();
}

int AnimationManager::GetMaxMovesetsFor(const std::string& category, int stanceIndex) { 
    
    if (stanceIndex < 0 || stanceIndex >= 4) {
        return 0;
    }
    // Procura a categoria no mapa
    auto it = _maxMovesetsPerCategory.find(category);
    if (it != _maxMovesetsPerCategory.end()) {
        // Se encontrou, retorna o valor para a stance específica
        return it->second[stanceIndex];
    }
    // Se não encontrou a categoria, não há movesets
    return 0;
}
// Adicione esta nova função em Hooks.cpp
int AnimationManager::GetMaxMovesetsForNPC(const std::string& category, int stanceIndex) {
    if (stanceIndex < 0 || stanceIndex >= 4) {
        return 0;
    }
    // Procura a categoria no novo mapa de NPCs
    auto it = _maxMovesetsPerCategory_NPC.find(category);
    if (it != _maxMovesetsPerCategory_NPC.end()) {
        // Se encontrou, retorna o valor para a stance específica
        return it->second[stanceIndex];
    }
    // Se não encontrou a categoria, não há movesets
    return 0;
}

const std::map<std::string, WeaponCategory>& AnimationManager::GetCategories() const { 
    return _categories; }

void AnimationManager::DrawCategoryUI(WeaponCategory& category) {
    ImGui::PushID(category.name.c_str());
    if (ImGui::CollapsingHeader(category.name.c_str())) {
        ImGui::BeginGroup(); 
        if (ImGui::BeginTabBar(std::string("StanceTabs_" + category.name).c_str())) {
            for (int i = 0; i < 4; ++i) {
                // Pega o nome atual da stance
                const char* currentStanceName = category.stanceNameBuffers[i].data();

                // Desenha a aba apenas com o nome
                bool tab_open = ImGui::BeginTabItem(currentStanceName);
             
                if (tab_open) {
                    category.activeInstanceIndex = i;
                    CategoryInstance& instance = category.instances[i];

                    std::map<SubAnimationInstance*, int> playlistNumbers;
                    std::map<SubAnimationInstance*, int> parentNumbersForChildren;
                    int currentPlaylistCounter = 1;
                    int lastValidParentNumber = 0;

                    for (auto& modInst : instance.modInstances) {
                        if (!modInst.isSelected) continue;  // Pula movesets desativados

                        for (auto& subInst : modInst.subAnimationInstances) {
                            if (!subInst.isSelected) continue;  // Pula sub-movesets desativados

                            // Sua lógica para determinar se é um "Pai"
                            bool isParent = !(subInst.pFront || subInst.pBack || subInst.pLeft || subInst.pRight ||
                                              subInst.pFrontRight || subInst.pFrontLeft || subInst.pBackRight ||
                                              subInst.pBackLeft || subInst.pRandom || subInst.pDodge);

                            if (isParent) {
                                lastValidParentNumber = currentPlaylistCounter;
                                playlistNumbers[&subInst] = currentPlaylistCounter;
                                currentPlaylistCounter++;
                            } else {
                                // É um filho, armazena o número do seu pai
                                parentNumbersForChildren[&subInst] = lastValidParentNumber;
                            }
                        }
                    }
                    if (ImGui::Button(LOC("edit_stance_name"))) {
                        _isEditStanceModalOpen = true;
                        _categoryToEdit = &category;
                        _stanceIndexToEdit = i;
                        strcpy_s(_editStanceNameBuffer, sizeof(_editStanceNameBuffer), currentStanceName);
                    }

                    // 2. Colocamos o botão "Adicionar Moveset" na mesma linha para criar uma barra de ações.
                    ImGui::SameLine();
                    // Botões de ação para a instância
                    if (ImGui::Button(LOC("add_moveset"))) {
                        _isAddModModalOpen = true;
                        _instanceToAddTo = &instance;
                        _modInstanceToAddTo = nullptr;
                    }
                    ImGui::Separator();

                    int modInstanceToRemove = -1;
                    // int playlistEntryCounter = 1;  // Contador apenas para "Pais"
                    //  Loop para os Movesets (ModInstance)
                    for (size_t mod_i = 0; mod_i < instance.modInstances.size(); ++mod_i) {
                        auto& modInstance = instance.modInstances[mod_i];
                        const auto& sourceMod = _allMods[modInstance.sourceModIndex];

                        ImGui::PushID(static_cast<int>(mod_i));
                        // --- INÍCIO DA CORREÇÃO DO BUG VISUAL ---

                        // 1. Salvamos o estado ANTES de desenhar qualquer coisa.
                        const bool isParentDisabled = !modInstance.isSelected;

                        // 2. Aplicamos a cor cinza se o estado for "desabilitado".
                        if (isParentDisabled) {
                            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle()->Colors[ImGuiCol_TextDisabled]);
                        }

                        // 3. Desenhamos todos os widgets do "Pai" (botão, checkbox, nome)
                        if (ImGui::Button("X")) modInstanceToRemove = static_cast<int>(mod_i);
                        ImGui::SameLine();
                        ImGui::Checkbox("##modselect", &modInstance.isSelected);
                        ImGui::SameLine();
                        bool node_open = ImGui::TreeNode(sourceMod.name.c_str());

                        // Drag and Drop para MOVESETS
                        if (ImGui::BeginDragDropSource()) {
                            ImGui::SetDragDropPayload("DND_MOD_INSTANCE", &mod_i, sizeof(size_t));
                            ImGui::Text("Mover moveset %s", sourceMod.name.c_str());
                            ImGui::EndDragDropSource();
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_MOD_INSTANCE")) {
                                size_t source_idx = *(const size_t*)payload->Data;
                                std::swap(instance.modInstances[source_idx], instance.modInstances[mod_i]);
                            }
                        }

                        if (node_open) {
                            if (ImGui::Button(LOC("add_animation"))) {
                                _isAddModModalOpen = true;
                                _modInstanceToAddTo = &modInstance;
                                _instanceToAddTo = nullptr;
                            }
                            // Estas variáveis agora controlam a lógica de agrupamento

                            // int lastParentNumber = 0;   // Armazena o número do último "Pai"
                            //  NOVO: Variável para marcar um sub-moveset para remoção
                            // int subInstanceToRemove = -1;

                            // Loop para os Sub-Movesets (SubAnimationInstance)
                            for (size_t sub_j = 0; sub_j < modInstance.subAnimationInstances.size(); ++sub_j) {
                                auto& subInstance = modInstance.subAnimationInstances[sub_j];
                                const auto& originMod = _allMods[subInstance.sourceModIndex];
                                const auto& originSubAnim = originMod.subAnimations[subInstance.sourceSubAnimIndex];

                                ImGui::PushID(static_cast<int>(sub_j));
                                // A cor do filho depende do seu próprio estado ou do estado do pai.
                                const bool isChildDisabled = !subInstance.isSelected || isParentDisabled;

                                if (isChildDisabled) {
                                    ImGui::PushStyleColor(ImGuiCol_Text,
                                                          ImGui::GetStyle()->Colors[ImGuiCol_TextDisabled]);
                                }

                                // NOVO: Define as flags da tabela. SizingFixedFit permite colunas de
                                // tamanho fixo.
                                ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
                                if (ImGui::BeginTable("sub_moveset_layout", 2, flags)) {
                                    // NOVO: Define as colunas. A primeira estica, a segunda é fixa.
                                    // O tamanho da segunda coluna é calculado para caber todos os
                                    // checkboxes.
                                    float checkbox_width = ImGui::GetFrameHeightWithSpacing() + 1000;  // 11 checkboxes
                                    ImGui::TableSetupColumn("Info", ImGuiTableColumnFlags_WidthStretch);
                                    ImGui::TableSetupColumn("Conditions", ImGuiTableColumnFlags_WidthFixed,
                                                            checkbox_width);

                                    // --- COLUNA 1: Informações Principais ---
                                    ImGui::TableNextColumn();

                                    ImGui::Checkbox("##subselect", &subInstance.isSelected);
                                    ImGui::SameLine();

                                    // NOVO: Agrupa o nome e as tags para que o Drag and Drop funcione
                                    // em ambos.
                                    ImGui::BeginGroup();

                                    // Lógica para criar a label principal (igual ao código original)
                                    std::string label;
                                    if (modInstance.isSelected && subInstance.isSelected) {
                                        if (playlistNumbers.count(&subInstance)) {
                                            label = std::format("[{}] {}", playlistNumbers.at(&subInstance),
                                                                originSubAnim.name);
                                        } else if (parentNumbersForChildren.count(&subInstance)) {
                                            int parentNum = parentNumbersForChildren.at(&subInstance);
                                            label = std::format(" -> [{}] {}", parentNum, originSubAnim.name);
                                        } else {
                                            label = originSubAnim.name;
                                        }
                                    } else {
                                        label = originSubAnim.name;
                                    }
                                    if (subInstance.sourceModIndex != modInstance.sourceModIndex) {
                                        label += std::format(" (by: {})", originMod.name);
                                    }

                                    // ALTERADO: Desenhamos o texto da label. Não usamos mais Selectable
                                    // aqui.
                                    ImGui::Selectable(label.c_str(), false, 0, ImVec2(0, ImGui::GetTextLineHeight()));

                                    // CORREÇÃO: O código de Drag and Drop AGORA funciona, pois está
                                    // atrelado ao
                                    // Selectable acima.
                                    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
                                        ImGui::SetDragDropPayload("DND_SUB_INSTANCE", &sub_j, sizeof(size_t));
                                        ImGui::Text("Mover %s", originSubAnim.name.c_str());
                                        ImGui::EndDragDropSource();
                                    }
                                    if (ImGui::BeginDragDropTarget()) {
                                        if (const ImGuiPayload* payload =
                                                ImGui::AcceptDragDropPayload("DND_SUB_INSTANCE")) {
                                            size_t source_idx = *(const size_t*)payload->Data;
                                            std::swap(modInstance.subAnimationInstances[source_idx],
                                                      modInstance.subAnimationInstances[sub_j]);
                                        }
                                    }

                                    // ALTERADO: As tags agora são desenhadas abaixo da label, dentro do
                                    // mesmo grupo. Elas usam SameLine() entre si para ficarem na mesma
                                    // linha.
                                    bool firstTag = true;
                                    if (originSubAnim.attackCount > 0) {
                                        if (!firstTag) ImGui::SameLine();
                                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[HitCombo: %d]",
                                                           originSubAnim.attackCount);
                                        firstTag = false;
                                    }
                                    if (originSubAnim.powerAttackCount > 0) {
                                        if (!firstTag) ImGui::SameLine();
                                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "[PA: %d]",
                                                           originSubAnim.powerAttackCount);
                                        firstTag = false;
                                    }
                                    if (originSubAnim.hasIdle) {
                                        if (!firstTag) ImGui::SameLine();
                                        ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "[Idle]");
                                        firstTag = false;
                                    }

                                    ImGui::EndGroup();  // Fim do grupo de Drag and Drop

                                    // --- COLUNA 2: Checkboxes de Condição ---
                                    ImGui::TableNextColumn();

                                    // MOVIDO: Todos os checkboxes agora estão na segunda coluna.
                                    // Eles usam SameLine() para se alinharem horizontalmente DENTRO da
                                    // coluna.
                                    ImGui::Checkbox("F", &subInstance.pFront);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("B", &subInstance.pBack);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("L", &subInstance.pLeft);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("R", &subInstance.pRight);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("FR", &subInstance.pFrontRight);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("FL", &subInstance.pFrontLeft);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("BR", &subInstance.pBackRight);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("BL", &subInstance.pBackLeft);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("Rnd", &subInstance.pRandom);
                                    ImGui::SameLine();
                                    ImGui::Checkbox("Movement", &subInstance.pDodge);

                                    ImGui::EndTable();
                                }
                                // --- FIM DA NOVA ESTRUTURA COM TABELA ---

                                if (isChildDisabled) {
                                    ImGui::PopStyleColor();
                                }

                                ImGui::PopID();
                            }

                            ImGui::TreePop();
                        }
                        if (isParentDisabled) {
                            ImGui::PopStyleColor();
                        }

                        ImGui::PopID();
                    }

                    if (modInstanceToRemove != -1) {
                        instance.modInstances.erase(instance.modInstances.begin() + modInstanceToRemove);
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::EndGroup();
    }
    ImGui::PopID();
}

void AnimationManager::DrawAnimationManager() {
    if (ImGui::Button(LOC("save"))) {
        SaveAllSettings();
  
    }
    ImGui::SameLine();
    ImGui::Checkbox(LOC("save_oldconditions"), &_preserveConditions);
    ImGui::Separator();

    // DrawAddModModal();

    if (_categories.empty()) {
        ImGui::Text("Nenhuma categoria de animação foi carregada.");
        return;
    }

    if(ImGui::BeginTabBar("WeaponTypeTabs")) {
        if (ImGui::BeginTabItem("Single-Wield")) {
            for (auto& pair : _categories) {
                WeaponCategory& category = pair.second;
                if (!category.isDualWield) {  // Filtro
                    DrawCategoryUI(pair.second);
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Dual-Wield")) {
            for (auto& pair : _categories) {
                WeaponCategory& category = pair.second;
                if (category.isDualWield) {  // Filtro
                    DrawCategoryUI(pair.second);
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

}
void AnimationManager::DrawNPCManager() {
    if (ImGui::Button(LOC("save"))) {
        SaveAllSettings();  // Chama a nova função de salvamento específica para NPCs
    }
    ImGui::Separator();

    if (_npcCategories.empty()) {
        ImGui::Text("Nenhuma categoria de animação foi carregada para NPCs.");
        return;
    }

    // Sistema de abas para separar Dual Wield, igual ao do Player
    if (ImGui::BeginTabBar("WeaponTypeTabs_NPC")) {
        if (ImGui::BeginTabItem("Single Wield")) {
            for (auto& pair : _npcCategories) {
                if (!pair.second.isDualWield) {
                    DrawNPCCategoryUI(pair.second);  // Chama o helper de UI específico para NPC
                }
            }
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Dual Wield")) {
            for (auto& pair : _npcCategories) {
                if (pair.second.isDualWield) {
                    DrawNPCCategoryUI(pair.second);
                }
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// Helper para a UI de Categoria do NPC
void AnimationManager::DrawNPCCategoryUI(WeaponCategory& category) {
    ImGui::PushID(category.name.c_str());
    if (ImGui::CollapsingHeader(category.name.c_str())) {
        // NPCs usam a instância 0 (Stance 0)
        CategoryInstance& instance = category.instances[0];

        // --- PONTO 2: Lógica para calcular a ordem dos movesets ---
        int playlistCounter = 1;
        std::map<const SubAnimationInstance*, int> playlistNumbers;
        for (auto& modInst : instance.modInstances) {
            if (modInst.isSelected) {
                for (auto& subInst : modInst.subAnimationInstances) {
                    if (subInst.isSelected) {
                        playlistNumbers[&subInst] = playlistCounter++;
                    }
                }
            }
        }
        // --- FIM DA LÓGICA DE ORDEM ---

        if (ImGui::Button(LOC("add_moveset"))) {
            _isAddModModalOpen = true;
            _instanceToAddTo = &instance;
            _modInstanceToAddTo = nullptr;
        }
        ImGui::Separator();

        int modInstanceToRemove = -1;
        for (size_t mod_i = 0; mod_i < instance.modInstances.size(); ++mod_i) {
            auto& modInstance = instance.modInstances[mod_i];
            const auto& sourceMod = _allMods[modInstance.sourceModIndex];

            ImGui::PushID(static_cast<int>(mod_i));

            const bool isParentDisabled = !modInstance.isSelected;
            if (isParentDisabled) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle()->Colors[ImGuiCol_TextDisabled]);
            }

            if (ImGui::Button("X")) modInstanceToRemove = static_cast<int>(mod_i);
            ImGui::SameLine();
            ImGui::Checkbox("##modselect", &modInstance.isSelected);
            ImGui::SameLine();
            bool node_open = ImGui::TreeNode(sourceMod.name.c_str());

            // --- PONTO 1: Adicionando Drag and Drop para os Movesets de NPC ---
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("DND_MOD_INSTANCE_NPC", &mod_i, sizeof(size_t));
                ImGui::Text("Mover moveset %s", sourceMod.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_MOD_INSTANCE_NPC")) {
                    size_t source_idx = *(const size_t*)payload->Data;
                    if (source_idx != mod_i) {
                        std::swap(instance.modInstances[source_idx], instance.modInstances[mod_i]);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            // --- FIM DO DRAG AND DROP ---

            if (node_open) {
                if (ImGui::Button(LOC("add_animation"))) {
                    _isAddModModalOpen = true;           // Abre o menu modal
                    _modInstanceToAddTo = &modInstance;  // Aponta para o moveset de NPC atual
                    _instanceToAddTo = nullptr;          // Limpa o ponteiro de instância geral
                    _userMovesetToAddTo = nullptr;       // Limpa o ponteiro de moveset do usuário
                }
                for (size_t sub_j = 0; sub_j < modInstance.subAnimationInstances.size(); ++sub_j) {
                    auto& subInstance = modInstance.subAnimationInstances[sub_j];
                    const auto& originMod = _allMods[subInstance.sourceModIndex];
                    const auto& originSubAnim = originMod.subAnimations[subInstance.sourceSubAnimIndex];

                    ImGui::PushID(static_cast<int>(sub_j));

                    const bool isChildDisabled = !subInstance.isSelected || isParentDisabled;
                    if (isChildDisabled) {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle()->Colors[ImGuiCol_TextDisabled]);
                    }

                    ImGui::Checkbox("##subselect", &subInstance.isSelected);
                    ImGui::SameLine();

                    // --- PONTO 2: Exibindo a ordem do sub-moveset ---
                    std::string label;
                    if (playlistNumbers.count(&subInstance)) {
                        label = std::format("[{}] {}", playlistNumbers.at(&subInstance), originSubAnim.name);
                    } else {
                        label = originSubAnim.name;
                    }

                    if (subInstance.sourceModIndex != modInstance.sourceModIndex) {
                        label += std::format(" (de: {})", originMod.name);
                    }
                    ImGui::Selectable(label.c_str(), false);
                    // --- FIM DA EXIBIÇÃO DA ORDEM ---

                    // --- PONTO 1: Adicionando Drag and Drop para os Sub-Movesets de NPC ---
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload("DND_SUB_INSTANCE_NPC", &sub_j, sizeof(size_t));
                        ImGui::Text("Mover %s", originSubAnim.name.c_str());
                        ImGui::EndDragDropSource();
                    }
                    if (ImGui::BeginDragDropTarget()) {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("DND_SUB_INSTANCE_NPC")) {
                            size_t source_idx = *(const size_t*)payload->Data;
                            if (source_idx != sub_j) {
                                std::swap(modInstance.subAnimationInstances[source_idx],
                                          modInstance.subAnimationInstances[sub_j]);
                            }
                        }
                        ImGui::EndDragDropTarget();
                    }
                    // --- FIM DO DRAG AND DROP ---

                    // --- PONTO 3: Adicionando Checkboxes de Random e Dodge ---
                    ImGui::SameLine();  // Alinha à direita
                    ImGui::Checkbox("Rnd", &subInstance.pRandom);
                    ImGui::SameLine();
                    ImGui::Checkbox("Movement", &subInstance.pDodge);
                    // --- FIM DAS CHECKBOXES ---

                    if (isChildDisabled) {
                        ImGui::PopStyleColor();
                    }

                    ImGui::PopID();
                }
                ImGui::TreePop();
            }

            if (isParentDisabled) {
                ImGui::PopStyleColor();
            }
            ImGui::PopID();
        }

        if (modInstanceToRemove != -1) {
            instance.modInstances.erase(instance.modInstances.begin() + modInstanceToRemove);
        }
    }
    ImGui::PopID();
}

void AnimationManager::SaveAllSettings() {
    SKSE::log::info("Iniciando salvamento global de todas as configurações...");
    SaveStanceNames();
    SaveCycleMovesets();
    SKSE::log::info("Gerando arquivos de condição para OAR...");
    std::map<std::filesystem::path, std::vector<FileSaveConfig>> fileUpdates;

    // 1. Loop através de cada CATEGORIA de arma
    for (auto& pair : _categories) {
        WeaponCategory& category = pair.second;

        // 2. Loop através de cada uma das 4 INSTÂNCIAS
        for (int i = 0; i < 4; ++i) {
            CategoryInstance& instance = category.instances[i];
            int playlistParentCounter = 1;  // Contador para os itens "Pai"
            int lastParentOrder = 0;        // Armazena o número do último "Pai"
            // 3. Loop através dos MOVESETS (ModInstance) na instância
            for (size_t mod_i = 0; mod_i < instance.modInstances.size(); ++mod_i) {
                ModInstance& modInstance = instance.modInstances[mod_i];

                // 4. Loop através dos SUB-MOVESETS (SubAnimationInstance)
                for (size_t sub_j = 0; sub_j < modInstance.subAnimationInstances.size(); ++sub_j) {
                    SubAnimationInstance& subInstance = modInstance.subAnimationInstances[sub_j];

                    // Salva apenas se tanto o sub-moveset quanto o moveset pai estiverem selecionados
                    if (modInstance.isSelected && subInstance.isSelected) {
                        const auto& sourceMod = _allMods[subInstance.sourceModIndex];
                        const auto& sourceSubAnim = sourceMod.subAnimations[subInstance.sourceSubAnimIndex];

                        FileSaveConfig config;
                        config.isNPC = false;
                        config.instance_index = i + 1;  // Instância é 1-4
                        config.category = &category;

                        // Copia o estado de todas as checkboxes para o config
                        config.pFront = subInstance.pFront;
                        config.pBack = subInstance.pBack;
                        config.pLeft = subInstance.pLeft;
                        config.pRight = subInstance.pRight;
                        config.pFrontRight = subInstance.pFrontRight;
                        config.pFrontLeft = subInstance.pFrontLeft;
                        config.pBackRight = subInstance.pBackRight;
                        config.pBackLeft = subInstance.pBackLeft;
                        config.pRandom = subInstance.pRandom;
                        config.pDodge = subInstance.pDodge;

                        // Determina se é um "Pai" (nenhuma checkbox de direção marcada) ou "Filho"
                        bool isParent = !(config.pFront || config.pBack || config.pLeft || config.pRight ||
                                          config.pFrontRight || config.pFrontLeft || config.pBackRight ||
                                          config.pBackLeft || config.pRandom || config.pDodge);

                        config.isParent = isParent;

                        if (isParent) {
                            lastParentOrder = playlistParentCounter;
                            config.order_in_playlist = playlistParentCounter++;
                        } else {
                            // Filhos herdam o número do último pai encontrado
                            config.order_in_playlist = lastParentOrder;
                        }

                        // Adiciona a configuração ao mapa, agrupada pelo caminho do arquivo
                        fileUpdates[sourceSubAnim.path].push_back(config);
                    }
                }
            }
        }
    }

    // Processar Configurações dos NPCs ---
    // Adicione este bloco logo após o final do loop de '_categories'.
    // É quase uma cópia do loop acima, mas para '_npcCategories'.
    SKSE::log::info("Coletando configurações de NPCs para salvamento...");
    for (auto& pair : _npcCategories) {
        WeaponCategory& category = pair.second;
        // NPCs usam apenas a stance 0 (índice 0)
        CategoryInstance& instance = category.instances[0];
        int playlistCounter = 1;

        for (auto& modInstance : instance.modInstances) {
            if (!modInstance.isSelected) continue;

            for (auto& subInstance : modInstance.subAnimationInstances) {
                if (!subInstance.isSelected) continue;

                const auto& sourceMod = _allMods[subInstance.sourceModIndex];
                const auto& sourceSubAnim = sourceMod.subAnimations[subInstance.sourceSubAnimIndex];

                FileSaveConfig config;
                config.isNPC = true;        // Flag que diferencia do Player
                config.instance_index = 0;  // Stance 0 para NPCs
                config.category = &category;
                config.isParent = true;  // Para NPCs, cada entrada é um "Pai" na playlist
                config.order_in_playlist = playlistCounter++;

                // Copiando flags relevantes para NPCs
                config.pRandom = subInstance.pRandom;
                config.pDodge = subInstance.pDodge;

                // Zerando flags direcionais para consistência
                config.pFront = config.pBack = config.pLeft = config.pRight = false;
                config.pFrontRight = config.pFrontLeft = config.pBackRight = config.pBackLeft = false;

                // Adiciona ao mesmo mapa de atualizações
                fileUpdates[sourceSubAnim.path].push_back(config);
            }
        }
    }

    // Agora, verifique todos os arquivos que já gerenciamos.
    // Se algum deles não estiver na lista de atualizações ativas,
    // significa que ele foi removido e precisa ser desativado.
    for (const auto& managedPath : _managedFiles) {
        // Se o arquivo não está no mapa de atualizações, adicione-o com um vetor vazio.
        if (fileUpdates.find(managedPath) == fileUpdates.end()) {
            fileUpdates[managedPath] = {};  // Adiciona para a fila de desativação
        }
    }
    // Adiciona todos os novos arquivos à lista de gerenciados para o futuro.
    for (const auto& pair : fileUpdates) {
        _managedFiles.insert(pair.first);
    }

    SKSE::log::info("{} arquivos de configuração serão modificados.", fileUpdates.size());
    for (const auto& updateEntry : fileUpdates) {
        UpdateOrCreateJson(updateEntry.first, updateEntry.second);
    }

    SKSE::log::info("Salvamento global concluído.");
    RE::DebugNotification("Todas as configurações foram salvas!");
    UpdateMaxMovesetCache();

    RecarregarAnimacoesOAR();
    _showRestartPopup = true;
}

void AnimationManager::UpdateOrCreateJson(const std::filesystem::path& jsonPath,
                                          const std::vector<FileSaveConfig>& configs) {
    rapidjson::Document doc;
    std::ifstream fileStream(jsonPath);
    if (fileStream) {
        std::string jsonContent((std::istreambuf_iterator<char>(fileStream)), std::istreambuf_iterator<char>());
        fileStream.close();
        if (doc.Parse(jsonContent.c_str()).HasParseError()) {
            SKSE::log::error("Erro de Parse ao ler {}. Criando um novo arquivo.", jsonPath.string());
            doc.SetObject();
        }
    } else {
        doc.SetObject();
    }

    if (!doc.IsObject()) doc.SetObject();
    auto& allocator = doc.GetAllocator();

    // ---> INÍCIO DA NOVA LÓGICA DE PRIORIDADE <---

    // 1. Lê a prioridade base do arquivo. Se não existir, usa um valor padrão (ex: 0).
    int basePriority = 2000000000;
    if (doc.HasMember("priority") && doc["priority"].IsInt()) {
        basePriority;  //= doc["priority"].GetInt();
    }

    // 2. Determina se esta animação está sendo usada como "mãe" em QUALQUER uma das configurações.
    bool isUsedAsParent = false;
    for (const auto& config : configs) {
        if (config.isParent) {
            isUsedAsParent = true;
            break;  // Se encontrarmos um uso como "mãe", já podemos parar.
        }
    }

    // 3. Define a prioridade final. Se for usada como mãe, mantém a base.
    //    Se for usada APENAS como filha, incrementa a prioridade para garantir que ela sobrescreva a mãe.
    int finalPriority = isUsedAsParent ? basePriority : basePriority + 1;

    // 4. Aplica a prioridade final ao documento JSON.
    if (doc.HasMember("priority")) {
        doc["priority"].SetInt(finalPriority);
    } else {
        doc.AddMember("priority", finalPriority, allocator);
    }

    rapidjson::Value oldConditions(rapidjson::kArrayType);
    if (_preserveConditions && doc.HasMember("conditions") && doc["conditions"].IsArray()) {
        for (auto& cond : doc["conditions"].GetArray()) {
            if (cond.IsObject() && cond.HasMember("comment") && cond["comment"] == "OAR_CYCLE_MANAGER_CONDITIONS") {
                // Pula o nosso próprio bloco ao preservar, pois ele será reescrito
                continue;
            }
            rapidjson::Value c;
            c.CopyFrom(cond, allocator);
            oldConditions.PushBack(c, allocator);
        }
    }

    if (doc.HasMember("conditions")) {
        doc["conditions"].SetArray();
    } else {
        doc.AddMember("conditions", rapidjson::Value(rapidjson::kArrayType), allocator);
    }
    rapidjson::Value& conditions = doc["conditions"];

    if (_preserveConditions && !oldConditions.Empty()) {
        rapidjson::Value oldConditionsBlock(rapidjson::kObjectType);
        oldConditionsBlock.AddMember("condition", "OR", allocator);
        oldConditionsBlock.AddMember("comment", "Old Conditions", allocator);
        oldConditionsBlock.AddMember("Conditions", oldConditions, allocator);
        conditions.PushBack(oldConditionsBlock, allocator);
    }

    // Passo 1: Mapear todas as direções usadas pelas "filhas" para cada "mãe" (playlist).
    // A chave do mapa é o 'order_in_playlist', o valor é um set com os números das direções.
    std::map<int, std::set<int>> childDirectionsByPlaylist;
    for (const auto& config : configs) {
        if (!config.isParent) {
            int playlistId = config.order_in_playlist;
            if (playlistId > 0) {  // Garante que é uma filha de uma playlist válida
                if (config.pFront) childDirectionsByPlaylist[playlistId].insert(1);
                if (config.pFrontRight) childDirectionsByPlaylist[playlistId].insert(2);
                if (config.pRight) childDirectionsByPlaylist[playlistId].insert(3);
                if (config.pBackRight) childDirectionsByPlaylist[playlistId].insert(4);
                if (config.pBack) childDirectionsByPlaylist[playlistId].insert(5);
                if (config.pBackLeft) childDirectionsByPlaylist[playlistId].insert(6);
                if (config.pLeft) childDirectionsByPlaylist[playlistId].insert(7);
                if (config.pFrontLeft) childDirectionsByPlaylist[playlistId].insert(8);
            }
        }
    }

    if (!configs.empty()) {
        rapidjson::Value masterOrBlock(rapidjson::kObjectType);
        masterOrBlock.AddMember("condition", "OR", allocator);
        masterOrBlock.AddMember("comment", "OAR_CYCLE_MANAGER_CONDITIONS", allocator);
        rapidjson::Value innerConditions(rapidjson::kArrayType);

        for (const auto& config : configs) {
            rapidjson::Value categoryAndBlock(rapidjson::kObjectType);
            categoryAndBlock.AddMember("condition", "AND", allocator);
            rapidjson::Value andConditions(rapidjson::kArrayType);

            {
                rapidjson::Value actorBase(rapidjson::kObjectType);
                actorBase.AddMember("condition", "IsActorBase", allocator);
                if (config.isNPC) {
                    actorBase.AddMember("negated", true, allocator);
                }
                rapidjson::Value actorBaseParams(rapidjson::kObjectType);
                actorBaseParams.AddMember("pluginName", "Skyrim.esm", allocator);
                actorBaseParams.AddMember("formID", "7", allocator);
                actorBase.AddMember("Actor base", actorBaseParams, allocator);
                andConditions.PushBack(actorBase, allocator);
            }
            {
                rapidjson::Value equippedType(rapidjson::kObjectType);
                equippedType.AddMember("condition", "IsEquippedType", allocator);
                rapidjson::Value typeVal(rapidjson::kObjectType);
                typeVal.AddMember("value", config.category->equippedTypeValue, allocator);
                equippedType.AddMember("Type", typeVal, allocator);
                equippedType.AddMember("Left hand", false, allocator);  // Condição da mão direita (sempre presente)
                andConditions.PushBack(equippedType, allocator);
            }

            // NOVA LÓGICA DE KEYWORDS
            // 1. Adiciona a keyword REQUERIDA para esta categoria (se houver)
            AddKeywordOrConditions(andConditions, config.category->keywords, false, allocator);
            AddCompetingKeywordExclusions(andConditions, config.category, false, allocator);

            // NOVA LÓGICA PARA MÃO ESQUERDA (DUAL WIELD vs UMA MÃO)
            if (config.category->isDualWield) {
                // Mão esquerda deve ter o mesmo tipo de arma
                rapidjson::Value equippedTypeL(rapidjson::kObjectType);
                equippedTypeL.AddMember("condition", "IsEquippedType", allocator);
                rapidjson::Value typeValL(rapidjson::kObjectType);
                typeValL.AddMember("value", config.category->equippedTypeValue, allocator);
                equippedTypeL.AddMember("Type", typeValL, allocator).AddMember("Left hand", true, allocator);
                andConditions.PushBack(equippedTypeL, allocator);

                // E também deve ter a mesma keyword (se aplicável)
                AddKeywordOrConditions(andConditions, config.category->keywords, false, allocator);
                AddCompetingKeywordExclusions(andConditions, config.category, false, allocator);
                // E não deve ter as keywords concorrentes

            } else {
                // Para armas de uma mão (não-dual), a mão esquerda deve estar vazia
                if (config.category->equippedTypeValue >= 1.0 && config.category->equippedTypeValue <= 4.0 ||
                    !config.category->keywords.empty()) {
                    rapidjson::Value equippedTypeL(rapidjson::kObjectType);
                    equippedTypeL.AddMember("condition", "IsEquippedType", allocator);
                    rapidjson::Value typeValL(rapidjson::kObjectType);
                    typeValL.AddMember("value", 0.0, allocator);  // 0.0 = Unarmed
                    equippedTypeL.AddMember("Type", typeValL, allocator).AddMember("Left hand", true, allocator);
                    andConditions.PushBack(equippedTypeL, allocator);
                }
            }

            AddCompareValuesCondition(andConditions, "cycle_instance", config.instance_index, allocator);
            //AddOcfWeaponExclusionConditions(andConditions, allocator);

            // Apenas adiciona a condição de ordem se for um "Pai"
            if (config.order_in_playlist > 0) {
                AddCompareValuesCondition(andConditions, "testarone", config.order_in_playlist, allocator);
                if (config.isParent) {
                    // LÓGICA DA MÃE: Adicionar condições negadas para cada direção de filha.
                    const auto& childDirs = childDirectionsByPlaylist[config.order_in_playlist];
                    if (!childDirs.empty()) {
                        for (int dirValue : childDirs) {
                            AddNegatedCompareValuesCondition(andConditions, "DirecionalCycleMoveset", dirValue,
                                                             allocator);
                        }
                    }
                } else {
                    // ---> LÓGICA DE ATIVAÇÃO CORRIGIDA (RANDOM + DIRECIONAL) <---

                    // 1. Adiciona a condição Random se a checkbox estiver marcada.
                    //    Esta condição é adicionada diretamente ao bloco AND principal.
                    if (config.pRandom) {
                        AddRandomCondition(andConditions, config.order_in_playlist, allocator);
                    }

                    // 2. Coleta as condições direcionais, independentemente da condição Random.
                    rapidjson::Value directionalOrConditions(rapidjson::kArrayType);
                    if (config.pFront)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 1, allocator);
                    if (config.pFrontRight)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 2, allocator);
                    if (config.pRight)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 3, allocator);
                    if (config.pBackRight)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 4, allocator);
                    if (config.pBack)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 5, allocator);
                    if (config.pBackLeft)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 6, allocator);
                    if (config.pLeft)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 7, allocator);
                    if (config.pFrontLeft)
                        AddCompareValuesCondition(directionalOrConditions, "DirecionalCycleMoveset", 8, allocator);

                    // 3. Se houver alguma condição direcional, cria o bloco OR e o adiciona
                    //    também ao bloco AND principal.
                    if (!directionalOrConditions.Empty()) {
                        rapidjson::Value orBlock(rapidjson::kObjectType);
                        orBlock.AddMember("condition", "OR", allocator);
                        orBlock.AddMember("Conditions", directionalOrConditions, allocator);
                        andConditions.PushBack(orBlock, allocator);
                    }
                }

                categoryAndBlock.AddMember("Conditions", andConditions, allocator);
                innerConditions.PushBack(categoryAndBlock, allocator);
            }
        }
        if (!innerConditions.Empty()) {
            masterOrBlock.AddMember("Conditions", innerConditions, allocator);
            conditions.PushBack(masterOrBlock, allocator);
        }
    }
    // Se a lista de configs ESTIVER VAZIA, geramos uma condição "kill switch".
    else {
        rapidjson::Value masterOrBlock(rapidjson::kObjectType);
        masterOrBlock.AddMember("condition", "OR", allocator);
        masterOrBlock.AddMember("comment", "OAR_CYCLE_MANAGER_CONDITIONS", allocator);
        rapidjson::Value innerConditions(rapidjson::kArrayType);

        rapidjson::Value andBlock(rapidjson::kObjectType);
        andBlock.AddMember("condition", "AND", allocator);
        rapidjson::Value andConditions(rapidjson::kArrayType);

        // Adiciona uma condição que sempre será falsa.
        // Assumindo que a variável "CycleMovesetDisable" nunca será 1.0 no seu behavior graph.
        AddCompareValuesCondition(andConditions, "CycleMovesetDisable", 1, allocator);

        andBlock.AddMember("Conditions", andConditions, allocator);
        innerConditions.PushBack(andBlock, allocator);
        masterOrBlock.AddMember("Conditions", innerConditions, allocator);
        conditions.PushBack(masterOrBlock, allocator);
    }

    FILE* fp;
    fopen_s(&fp, jsonPath.string().c_str(), "wb");
    if (!fp) {
        SKSE::log::error("Falha ao abrir o arquivo para escrita: {}", jsonPath.string());
        return;
    }
    char writeBuffer[65536];
    rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
    rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
    doc.Accept(writer);
    fclose(fp);
}

// ATUALIZADO: Apenas uma pequena modificação para garantir que o 'value' é tratado como double.
void AnimationManager::AddCompareValuesCondition(rapidjson::Value& conditionsArray, const std::string& graphVarName,
                                                 int value, rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value newCompare(rapidjson::kObjectType);
    newCompare.AddMember("condition", "CompareValues", allocator);
    newCompare.AddMember("requiredVersion", "1.0.0.0", allocator);
    rapidjson::Value valueA(rapidjson::kObjectType);
    valueA.AddMember("value", value, allocator);  
    newCompare.AddMember("Value A", valueA, allocator);
    newCompare.AddMember("Comparison", "==", allocator);
    rapidjson::Value valueB(rapidjson::kObjectType);
    valueB.AddMember("graphVariable", rapidjson::Value(graphVarName.c_str(), allocator), allocator);
    valueB.AddMember("graphVariableType", "Int", allocator);
    newCompare.AddMember("Value B", valueB, allocator);
    conditionsArray.PushBack(newCompare, allocator);
}

// NOVA FUNÇÃO HELPER: Adiciona uma condição "CompareValues" para um valor booleano.
// Usada para verificar as checkboxes de movimento (F, B, L, R, etc.).
void AnimationManager::AddCompareBoolCondition(rapidjson::Value& conditionsArray, const std::string& graphVarName,
                                               bool value, rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value newCompare(rapidjson::kObjectType);
    newCompare.AddMember("condition", "CompareValues", allocator);
    newCompare.AddMember("requiredVersion", "1.0.0.0", allocator);

    rapidjson::Value valueA(rapidjson::kObjectType);
    valueA.AddMember("value", value, allocator);
    newCompare.AddMember("Value A", valueA, allocator);

    newCompare.AddMember("Comparison", "==", allocator);

    rapidjson::Value valueB(rapidjson::kObjectType);
    valueB.AddMember("graphVariable", rapidjson::Value(graphVarName.c_str(), allocator), allocator);
    valueB.AddMember("graphVariableType", "bool", allocator);  // O tipo aqui é "bool"
    newCompare.AddMember("Value B", valueB, allocator);

    conditionsArray.PushBack(newCompare, allocator);
}

void AnimationManager::AddRandomCondition(rapidjson::Value& conditionsArray, int value,
                                          rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value newRandom(rapidjson::kObjectType);
    newRandom.AddMember("condition", "Random", allocator);
    newRandom.AddMember("requiredVersion", "2.3.0.0", allocator);

    rapidjson::Value state(rapidjson::kObjectType);
    state.AddMember("scope", "Local", allocator);
    state.AddMember("shouldResetOnLoopOrEcho", true, allocator);
    newRandom.AddMember("State", state, allocator);

    rapidjson::Value minVal(rapidjson::kObjectType);
    minVal.AddMember("value", static_cast<double>(value), allocator);
    newRandom.AddMember("Minimum random value", minVal, allocator);

    rapidjson::Value maxVal(rapidjson::kObjectType);
    maxVal.AddMember("value", static_cast<double>(value), allocator);
    newRandom.AddMember("Maximum random value", maxVal, allocator);

    newRandom.AddMember("Comparison", "==", allocator);

    rapidjson::Value numVal(rapidjson::kObjectType);
    numVal.AddMember("graphVariable", "CycleMovesetsRandom", allocator);
    numVal.AddMember("graphVariableType", "Float", allocator);
    newRandom.AddMember("Numeric value", numVal, allocator);

    conditionsArray.PushBack(newRandom, allocator);
}

// Toda a parte de user ta ca pra baixo

std::optional<size_t> AnimationManager::FindModIndexByName(const std::string& name) {
    for (size_t i = 0; i < _allMods.size(); ++i) {
        if (_allMods[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<size_t> AnimationManager::FindSubAnimIndexByName(size_t modIdx, const std::string& name) {
    if (modIdx >= _allMods.size()) return std::nullopt;
    const auto& modDef = _allMods[modIdx];
    for (size_t i = 0; i < modDef.subAnimations.size(); ++i) {
        if (modDef.subAnimations[i].name == name) {
            return i;
        }
    }
    return std::nullopt;
}

void AnimationManager::UpdateMaxMovesetCache() {
    SKSE::log::info("Atualizando cache de contagem máxima de movesets...");
    _maxMovesetsPerCategory.clear();

    for (auto& pair : _categories) {
        WeaponCategory& category = pair.second;
        std::array<int, 4> counts = {0, 0, 0, 0};

        for (int i = 0; i < 4; ++i) {  // Loop através das 4 stances (0 a 3)
            CategoryInstance& instance = category.instances[i];
            int parentMovesetCount = 0;

            // A lógica agora precisa ir mais fundo, para dentro dos sub-movesets.
            for (auto& modInst : instance.modInstances) {
                // Pula o pacote de moveset inteiro se ele estiver desmarcado.
                if (!modInst.isSelected) continue;

                // Agora, iteramos através de cada sub-animação dentro do pacote.
                for (auto& subInst : modInst.subAnimationInstances) {
                    // Pula a sub-animação se ela estiver desmarcada.
                    if (!subInst.isSelected) continue;

                    // A condição que define um "Pai" é a ausência de qualquer condição direcional.
                    // Esta lógica DEVE ser idêntica à usada na função SaveAllSettings para garantir consistência.
                    bool isParent = !(subInst.pFront || subInst.pBack || subInst.pLeft || subInst.pRight ||
                                      subInst.pFrontRight || subInst.pFrontLeft || subInst.pBackRight ||
                                      subInst.pBackLeft || subInst.pRandom || subInst.pDodge);

                    // Se for um "Pai", nós o contamos para o total da playlist.
                    if (isParent) {
                        parentMovesetCount++;
                    }
                }
            }
            counts[i] = parentMovesetCount;
        }

        _maxMovesetsPerCategory[category.name] = counts;
        SKSE::log::info("Categoria '{}' tem contagens: [Stance 1: {}], [Stance 2: {}], [Stance 3: {}], [Stance 4: {}]",
                        category.name, counts[0], counts[1], counts[2], counts[3]);
    }
    // --- NOVO: Cache dos NPCs ---
    _maxMovesetsPerCategory_NPC.clear();
    for (auto& pair : _npcCategories) {
        WeaponCategory& category = pair.second;

        // NPCs usam apenas a stance/instance 0
        CategoryInstance& instance = category.instances[0];
        int npcMovesetCount = 0;

        for (auto& modInst : instance.modInstances) {
            if (modInst.isSelected) {
                for (auto& subInst : modInst.subAnimationInstances) {
                    if (subInst.isSelected) {
                        // Para NPCs, cada sub-moveset selecionado conta como 1
                        npcMovesetCount++;
                    }
                }
            }
        }

        // Armazena a contagem na posição 0 do array (correspondente à stanceIndex 0)
        std::array<int, 4> npc_counts = {npcMovesetCount, 0, 0, 0};
        _maxMovesetsPerCategory_NPC[category.name] = npc_counts;
        SKSE::log::info("Categoria '{}' (NPC) tem contagem: {}", category.name, npcMovesetCount);
    }

    SKSE::log::info("Cache de contagem máxima de movesets (Player & NPC) foi atualizado.");
}



void AnimationManager::AddNegatedCompareValuesCondition(rapidjson::Value& conditionsArray,
                                                        const std::string& graphVarName, int value,
                                                        rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value newCompare(rapidjson::kObjectType);
    newCompare.AddMember("condition", "CompareValues", allocator);

    // ---> A ÚNICA DIFERENÇA ESTÁ AQUI <---
    newCompare.AddMember("negated", true, allocator);

    newCompare.AddMember("requiredVersion", "1.0.0.0", allocator);
    rapidjson::Value valueA(rapidjson::kObjectType);
    valueA.AddMember("value", value, allocator);
    newCompare.AddMember("Value A", valueA, allocator);
    newCompare.AddMember("Comparison", "==", allocator);
    rapidjson::Value valueB(rapidjson::kObjectType);
    valueB.AddMember("graphVariable", rapidjson::Value(graphVarName.c_str(), allocator), allocator);
    valueB.AddMember("graphVariableType", "Int", allocator);
    newCompare.AddMember("Value B", valueB, allocator);
    conditionsArray.PushBack(newCompare, allocator);
}

// Função auxiliar para criar e adicionar o bloco de condição complexo
void AnimationManager::AddOcfWeaponExclusionConditions(rapidjson::Value& parentArray,
                                                       rapidjson::Document::AllocatorType& allocator) {
    rapidjson::Value mainAndBlock(rapidjson::kObjectType);
    mainAndBlock.AddMember("condition", "AND", allocator);
    mainAndBlock.AddMember("requiredVersion", "1.0.0.0", allocator);

    rapidjson::Value innerConditions(rapidjson::kArrayType);

    // Lista dos editorIDs das armas a serem excluídas
    const std::vector<const char*> keywords = {
        "OCF_WeapTypeRapier1H", "OCF_WeapTypeRapier2H",    "OCF_WeapTypeKatana1H",   "OCF_WeapTypeKatana2H",
        "OCF_WeapTypePike1H",   "OCF_WeapTypePike2H",      "OCF_WeapTypeHalberd2H",  "OCF_WeapTypeHalberd1H",
        "OCF_WeapTypeClaw1H",   "OCF_WeapTypeTwinblade1H", "OCF_WeapTypeTwinblade2H"};

    // Função lambda para criar uma condição "IsEquippedHasKeyword" negada
    auto createNegatedKeywordCondition = [&](const char* editorID, bool isLeftHand) {
        rapidjson::Value condition(rapidjson::kObjectType);
        condition.AddMember("condition", "IsEquippedHasKeyword", allocator);
        condition.AddMember("requiredVersion", "1.0.0.0", allocator);
        condition.AddMember("negated", true, allocator);

        rapidjson::Value keywordObj(rapidjson::kObjectType);
        keywordObj.AddMember("editorID", rapidjson::StringRef(editorID), allocator);
        condition.AddMember("Keyword", keywordObj, allocator);

        condition.AddMember("Left hand", isLeftHand, allocator);
        return condition;
    };

    // Adiciona as condições para a maioria das armas (mão direita e esquerda)
    for (const auto& keyword : keywords) {
        innerConditions.PushBack(createNegatedKeywordCondition(keyword, false), allocator);  // Left hand: false
        innerConditions.PushBack(createNegatedKeywordCondition(keyword, true), allocator);   // Left hand: true
    }

    // Casos especiais do Quarterstaff que não seguem o padrão par
    innerConditions.PushBack(createNegatedKeywordCondition("OCF_WeapTypeQuarterstaff2H", false), allocator);
    innerConditions.PushBack(createNegatedKeywordCondition("OCF_WeapTypeQuarterstaff1H", true), allocator);

    mainAndBlock.AddMember("Conditions", innerConditions, allocator);
    parentArray.PushBack(mainAndBlock, allocator);
}

void AnimationManager::AddCompetingKeywordExclusions(rapidjson::Value& parentArray,
                                                     const WeaponCategory* currentCategory, bool isLeftHand,
                                                     rapidjson::Document::AllocatorType& allocator) {
    // 1. Coleta todas as keywords concorrentes primeiro
    std::vector<std::string> competingKeywords;
    for (const auto& pair : _categories) {
        const WeaponCategory& otherCategory = pair.second;
        if (otherCategory.name != currentCategory->name &&
            otherCategory.equippedTypeValue == currentCategory->equippedTypeValue && !otherCategory.keywords.empty()) {
            // Adiciona todas as keywords da categoria concorrente à lista de exclusão
            competingKeywords.insert(competingKeywords.end(), otherCategory.keywords.begin(),
                                     otherCategory.keywords.end());
        }
    }

    if (competingKeywords.empty()) {
        return;
    }

    rapidjson::Value exclusionAndBlock(rapidjson::kObjectType);
    exclusionAndBlock.AddMember("condition", "AND", allocator);
    exclusionAndBlock.AddMember("comment", "Exclude competing weapon keywords", allocator);
    rapidjson::Value innerExclusionConditions(rapidjson::kArrayType);

    for (const auto& keyword : competingKeywords) {
        // A lógica interna permanece a mesma, pois AddKeywordCondition já lida com uma keyword por vez
        AddKeywordCondition(innerExclusionConditions, keyword, isLeftHand, true, allocator);
    }

    exclusionAndBlock.AddMember("Conditions", innerExclusionConditions, allocator);
    parentArray.PushBack(exclusionAndBlock, allocator);
}


void AnimationManager::SaveCycleMovesets() {
    SKSE::log::info("Iniciando salvamento descentralizado no formato Player/NPC...");

    std::map<std::filesystem::path, std::unique_ptr<rapidjson::Document>> documents;
    std::set<std::filesystem::path> requiredFiles;

    auto processActorCategories = [&](const std::map<std::string, WeaponCategory>& sourceCategories,
                                      const std::string& actorName) {
        for (const auto& pair : sourceCategories) {
            const WeaponCategory& category = pair.second;
            for (int i = 0; i < 4; ++i) {  // Stances
                const CategoryInstance& instance = category.instances[i];
                for (const auto& modInst : instance.modInstances) {
                    if (!modInst.isSelected) continue;
                    const auto& sourceMod = _allMods[modInst.sourceModIndex];
                    for (const auto& subInst : modInst.subAnimationInstances) {
                        if (!subInst.isSelected) continue;

                        const auto& animOriginMod = _allMods[subInst.sourceModIndex];
                        const auto& animOriginSub = animOriginMod.subAnimations[subInst.sourceSubAnimIndex];
                        std::filesystem::path cycleJsonPath = animOriginSub.path.parent_path() / "CycleMoveset.json";
                        requiredFiles.insert(cycleJsonPath);

                        if (documents.find(cycleJsonPath) == documents.end()) {
                            documents[cycleJsonPath] = std::make_unique<rapidjson::Document>();
                            documents[cycleJsonPath]->SetArray();
                        }
                        rapidjson::Document& doc = *documents[cycleJsonPath];
                        auto& allocator = doc.GetAllocator();

                        rapidjson::Value* actorObj = nullptr;
                        for (auto& item : doc.GetArray()) {
                            if (item.IsObject() && item.HasMember("Name") && item["Name"].GetString() == actorName) {
                                actorObj = &item;
                                break;
                            }
                        }
                        if (!actorObj) {
                            rapidjson::Value newActorObj(rapidjson::kObjectType);
                            newActorObj.AddMember("Name", rapidjson::Value(actorName.c_str(), allocator), allocator);
                            newActorObj.AddMember("Menu", rapidjson::kArrayType, allocator);
                            doc.PushBack(newActorObj, allocator);
                            actorObj = &doc.GetArray()[doc.Size() - 1];
                        }

                        rapidjson::Value& menuArray = (*actorObj)["Menu"];
                        rapidjson::Value* categoryObj = nullptr;
                        for (auto& item : menuArray.GetArray()) {
                            if (item.IsObject() && item.HasMember("Category") &&
                                item["Category"].GetString() == category.name) {
                                categoryObj = &item;
                                break;
                            }
                        }
                        if (!categoryObj) {
                            rapidjson::Value newCategoryObj(rapidjson::kObjectType);
                            newCategoryObj.AddMember("Category", rapidjson::Value(category.name.c_str(), allocator),
                                                     allocator);
                            newCategoryObj.AddMember("stances", rapidjson::kArrayType, allocator);
                            menuArray.PushBack(newCategoryObj, allocator);
                            categoryObj = &menuArray.GetArray()[menuArray.Size() - 1];
                        }

                        rapidjson::Value& stancesArray = (*categoryObj)["stances"];
                        rapidjson::Value* stanceObj = nullptr;
                        for (auto& item : stancesArray.GetArray()) {
                            // CORREÇÃO DO ERRO DE COMPILAÇÃO AQUI:
                            if (item.IsObject() && item["index"].GetInt() == (i + 1) && item.HasMember("name") &&
                                strcmp(item["name"].GetString(), sourceMod.name.c_str()) == 0) {
                                stanceObj = &item;
                                break;
                            }
                        }
                        if (!stanceObj) {
                            rapidjson::Value newStanceObj(rapidjson::kObjectType);
                            newStanceObj.AddMember("index", i + 1, allocator);
                            newStanceObj.AddMember("type", "moveset", allocator);
                            newStanceObj.AddMember("name", rapidjson::Value(sourceMod.name.c_str(), allocator),
                                                   allocator);
                            newStanceObj.AddMember("animations", rapidjson::kArrayType, allocator);
                            stancesArray.PushBack(newStanceObj, allocator);
                            stanceObj = &stancesArray.GetArray()[stancesArray.Size() - 1];
                        }

                        rapidjson::Value& animationsArray = (*stanceObj)["animations"];
                        rapidjson::Value animObj(rapidjson::kObjectType);
                        animObj.AddMember("index", 1, allocator);
                        animObj.AddMember("sourceModName", rapidjson::Value(animOriginMod.name.c_str(), allocator),
                                          allocator);
                        animObj.AddMember("sourceSubName", rapidjson::Value(animOriginSub.name.c_str(), allocator),
                                          allocator);
                        animObj.AddMember("sourceConfigPath",
                                          rapidjson::Value(animOriginSub.path.string().c_str(), allocator), allocator);
                        animObj.AddMember("pFront", subInst.pFront, allocator);
                        animObj.AddMember("pBack", subInst.pBack, allocator);
                        animObj.AddMember("pLeft", subInst.pLeft, allocator);
                        animObj.AddMember("pRight", subInst.pRight, allocator);
                        animObj.AddMember("pFrontRight", subInst.pFrontRight, allocator);
                        animObj.AddMember("pFrontLeft", subInst.pFrontLeft, allocator);
                        animObj.AddMember("pBackRight", subInst.pBackRight, allocator);
                        animObj.AddMember("pBackLeft", subInst.pBackLeft, allocator);
                        animObj.AddMember("pRandom", subInst.pRandom, allocator);
                        animObj.AddMember("pDodge", subInst.pDodge, allocator);
                        animationsArray.PushBack(animObj, allocator);
                    }
                }
            }
        }
    };

    processActorCategories(_categories, "Player");
    processActorCategories(_npcCategories, "NPCs");

    SKSE::log::info("Escrevendo {} arquivos CycleMoveset.json...", documents.size());
    for (const auto& pair : documents) {
        const auto& path = pair.first;
        const auto& doc = pair.second;
        FILE* fp;
        fopen_s(&fp, path.string().c_str(), "wb");
        if (fp) {
            char writeBuffer[65536];
            rapidjson::FileWriteStream os(fp, writeBuffer, sizeof(writeBuffer));
            rapidjson::PrettyWriter<rapidjson::FileWriteStream> writer(os);
            doc->Accept(writer);
            fclose(fp);
        } else {
            SKSE::log::error("Falha ao abrir para escrita: {}", path.string());
        }
    }

    const std::filesystem::path oarRootPath = "Data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer";
    if (std::filesystem::exists(oarRootPath)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(oarRootPath)) {
            if (entry.is_regular_file() && entry.path().filename() == "CycleMoveset.json") {
                if (requiredFiles.find(entry.path()) == requiredFiles.end()) {
                    SKSE::log::info("Removendo arquivo órfão: {}", entry.path().string());
                    std::filesystem::remove(entry.path());
                }
            }
        }
    }
    SKSE::log::info("Salvamento de {} arquivos CycleMoveset.json concluído.", documents.size());
}

// VERSÃO CORRIGIDA E FUNCIONAL
void AnimationManager::LoadCycleMovesets() {
    SKSE::log::info("Iniciando carregamento descentralizado de arquivos CycleMoveset.json...");

    for (auto& pair : _categories) {
        for (auto& instance : pair.second.instances) instance.modInstances.clear();
    }
    for (auto& pair : _npcCategories) {
        for (auto& instance : pair.second.instances) instance.modInstances.clear();
    }

    const std::filesystem::path oarRootPath = "Data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer";
    if (!std::filesystem::exists(oarRootPath)) {
        SKSE::log::warn("Diretório do OpenAnimationReplacer não encontrado.");
        return;
    }

    int filesFound = 0;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(oarRootPath)) {
        if (entry.is_regular_file() && entry.path().filename() == "CycleMoveset.json") {
            filesFound++;

            FILE* fp;
            fopen_s(&fp, entry.path().string().c_str(), "rb");
            if (!fp) continue;

            char readBuffer[65536];
            rapidjson::FileReadStream is(fp, readBuffer, sizeof(readBuffer));
            rapidjson::Document doc;
            doc.ParseStream(is);
            fclose(fp);

            if (doc.HasParseError() || !doc.IsArray()) {
                SKSE::log::warn("Arquivo mal formatado, pulando: {}", entry.path().string());
                continue;
            }

            for (const auto& entity : doc.GetArray()) {
                if (!entity.IsObject() || !entity.HasMember("Name") || !entity.HasMember("Menu")) continue;

                std::string name = entity["Name"].GetString();
                const rapidjson::Value& menu = entity["Menu"];
                if (!menu.IsArray()) continue;

                std::map<std::string, WeaponCategory>* targetCategories = nullptr;
                if (name == "Player") {
                    targetCategories = &_categories;
                } else if (name == "NPCs") {
                    targetCategories = &_npcCategories;
                }

                if (targetCategories) {
                    for (const auto& categoryJson : menu.GetArray()) {
                        if (!categoryJson.IsObject() || !categoryJson.HasMember("Category") ||
                            !categoryJson.HasMember("stances"))
                            continue;
                        std::string categoryName = categoryJson["Category"].GetString();
                        auto categoryIt = targetCategories->find(categoryName);
                        if (categoryIt == targetCategories->end()) continue;

                        WeaponCategory& category = categoryIt->second;
                        const auto& stancesArray = categoryJson["stances"].GetArray();

                        for (const auto& stanceJson : stancesArray) {
                            if (!stanceJson.IsObject() || !stanceJson.HasMember("index") ||
                                !stanceJson.HasMember("name"))
                                continue;
                            int stanceIndex = stanceJson["index"].GetInt();
                            if (stanceIndex < 1 || stanceIndex > 4) continue;

                            CategoryInstance& targetInstance = category.instances[stanceIndex - 1];
                            std::string movesetName = stanceJson["name"].GetString();
                            auto modIdxOpt = FindModIndexByName(movesetName);
                            if (!modIdxOpt) continue;

                            ModInstance* modInstancePtr = nullptr;
                            for (auto& mi : targetInstance.modInstances) {
                                if (mi.sourceModIndex == *modIdxOpt) {
                                    modInstancePtr = &mi;
                                    break;
                                }
                            }
                            if (!modInstancePtr) {
                                targetInstance.modInstances.emplace_back();
                                modInstancePtr = &targetInstance.modInstances.back();
                                modInstancePtr->sourceModIndex = *modIdxOpt;
                            }

                            if (stanceJson.HasMember("animations") && stanceJson["animations"].IsArray()) {
                                // CORREÇÃO DO WARNING: usar a variável animJson
                                for (const auto& animJson : stanceJson["animations"].GetArray()) {
                                    SubAnimationInstance newSubInstance;

                                    // Preenchendo a SubAnimationInstance a partir do animJson...
                                    // (Esta parte do seu código original estava faltando, adicionei de volta)
                                    std::string subModName = animJson["sourceModName"].GetString();
                                    std::string subAnimName = animJson["sourceSubName"].GetString();

                                    auto subModIdxOpt = FindModIndexByName(subModName);
                                    if (subModIdxOpt) {
                                        newSubInstance.sourceModIndex = *subModIdxOpt;
                                        auto subAnimIdxOpt = FindSubAnimIndexByName(*subModIdxOpt, subAnimName);
                                        if (subAnimIdxOpt) {
                                            newSubInstance.sourceSubAnimIndex = *subAnimIdxOpt;
                                        } else {
                                            continue;
                                        }
                                    } else {
                                        continue;
                                    }

                                    if (animJson.HasMember("pFront"))
                                        newSubInstance.pFront = animJson["pFront"].GetBool();
                                    if (animJson.HasMember("pBack")) newSubInstance.pBack = animJson["pBack"].GetBool();
                                    if (animJson.HasMember("pLeft")) newSubInstance.pLeft = animJson["pLeft"].GetBool();
                                    if (animJson.HasMember("pRight"))
                                        newSubInstance.pRight = animJson["pRight"].GetBool();
                                    if (animJson.HasMember("pFrontRight"))
                                        newSubInstance.pFrontRight = animJson["pFrontRight"].GetBool();
                                    if (animJson.HasMember("pFrontLeft"))
                                        newSubInstance.pFrontLeft = animJson["pFrontLeft"].GetBool();
                                    if (animJson.HasMember("pBackRight"))
                                        newSubInstance.pBackRight = animJson["pBackRight"].GetBool();
                                    if (animJson.HasMember("pBackLeft"))
                                        newSubInstance.pBackLeft = animJson["pBackLeft"].GetBool();
                                    if (animJson.HasMember("pRandom"))
                                        newSubInstance.pRandom = animJson["pRandom"].GetBool();
                                    if (animJson.HasMember("pDodge"))
                                        newSubInstance.pDodge = animJson["pDodge"].GetBool();

                                    modInstancePtr->subAnimationInstances.push_back(newSubInstance);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    SKSE::log::info("Carregamento descentralizado concluído. {} arquivos processados.", filesFound);
    UpdateMaxMovesetCache();
}

void AnimationManager::SaveNPCSettings() {
    SKSE::log::info("Iniciando salvamento das configurações dos NPCs...");
    std::map<std::filesystem::path, std::vector<FileSaveConfig>> fileUpdates;

    for (auto& pair : _npcCategories) {
        WeaponCategory& category = pair.second;
        // NPCs usam apenas a stance 0
        CategoryInstance& instance = category.instances[0];
        int playlistParentCounter = 1;

        for (auto& modInstance : instance.modInstances) {
            if (!modInstance.isSelected) continue;

            for (auto& subInstance : modInstance.subAnimationInstances) {
                if (!subInstance.isSelected) continue;

                const auto& sourceMod = _allMods[subInstance.sourceModIndex];
                const auto& sourceSubAnim = sourceMod.subAnimations[subInstance.sourceSubAnimIndex];

                FileSaveConfig config;
                config.isNPC = true;        // Define a flag para NPC
                config.instance_index = 0;  // Stance 0 para NPCs
                config.category = &category;
                config.isParent = true;  // NPCs não têm direcionais, então tudo é "Pai"
                config.order_in_playlist = playlistParentCounter++;

                fileUpdates[sourceSubAnim.path].push_back(config);
            }
        }
    }

    SKSE::log::info("{} arquivos de configuração de NPC serão modificados.", fileUpdates.size());
    for (const auto& updateEntry : fileUpdates) {
        // A mesma função UpdateOrCreateJson é chamada!
        UpdateOrCreateJson(updateEntry.first, updateEntry.second);
    }
    RE::DebugNotification("Configurações dos NPCs salvas!");
}

void AnimationManager::AddKeywordCondition(rapidjson::Value& parentArray, const std::string& editorID, bool isLeftHand,
                                           bool negated, rapidjson::Document::AllocatorType& allocator) {
    if (editorID.empty()) return;  // Não faz nada se a keyword for vazia

    rapidjson::Value condition(rapidjson::kObjectType);
    condition.AddMember("condition", "IsEquippedHasKeyword", allocator);
    condition.AddMember("requiredVersion", "1.0.0.0", allocator);

    // Adiciona a flag "negated" se necessário
    if (negated) {
        condition.AddMember("negated", true, allocator);
    }

    rapidjson::Value keywordObj(rapidjson::kObjectType);
    keywordObj.AddMember("editorID", rapidjson::Value(editorID.c_str(), allocator), allocator);
    condition.AddMember("Keyword", keywordObj, allocator);

    condition.AddMember("Left hand", isLeftHand, allocator);
    parentArray.PushBack(condition, allocator);
}

void AnimationManager::AddKeywordOrConditions(rapidjson::Value& parentArray, const std::vector<std::string>& keywords,
                                              bool isLeftHand, rapidjson::Document::AllocatorType& allocator) {
    // Se não há keywords para checar, não faz nada.
    if (keywords.empty()) {
        return;
    }

    // Se houver apenas UMA keyword, não precisamos de um bloco OR. Adicionamos a condição diretamente.
    if (keywords.size() == 1) {
        AddKeywordCondition(parentArray, keywords[0], isLeftHand, false, allocator);
        return;
    }

    // Se houver mais de uma keyword, criamos o bloco OR
    rapidjson::Value orBlock(rapidjson::kObjectType);
    orBlock.AddMember("condition", "OR", allocator);
    orBlock.AddMember("comment", "Matches any of the required keywords", allocator);

    rapidjson::Value innerOrConditions(rapidjson::kArrayType);

    // Adiciona cada keyword como uma condição dentro do bloco OR
    for (const auto& keyword : keywords) {
        AddKeywordCondition(innerOrConditions, keyword, isLeftHand, false, allocator);
    }

    orBlock.AddMember("Conditions", innerOrConditions, allocator);
    parentArray.PushBack(orBlock, allocator);
}

// Adicione estas novas funções em Hooks.cpp

// Função para buscar o nome da stance
std::string AnimationManager::GetStanceName(const std::string& categoryName, int stanceIndex) {
    if (stanceIndex < 0 || stanceIndex >= 4) {
        return "Stance Inválida";
    }
    auto it = _categories.find(categoryName);
    if (it != _categories.end()) {
        return it->second.stanceNames[stanceIndex];
    }
    return std::to_string(stanceIndex + 1);  // Fallback
}

// Função para buscar o nome do moveset
std::string AnimationManager::GetCurrentMovesetName(const std::string& categoryName, int stanceIndex,
                                                    int movesetIndex) {
    if (movesetIndex <= 0) {
        return "Nenhum";
    }

    auto it = _categories.find(categoryName);
    if (it == _categories.end()) {
        return "Categoria não encontrada";
    }

    WeaponCategory& category = it->second;
    if (stanceIndex < 0 || stanceIndex >= 4) {
        return "Stance inválida";
    }

    CategoryInstance& instance = category.instances[stanceIndex];
    int parentCounter = 0;

    // A lógica de iteração DEVE ser idêntica à de UpdateMaxMovesetCache e SaveAllSettings
    for (auto& modInst : instance.modInstances) {
        if (!modInst.isSelected) continue;

        for (auto& subInst : modInst.subAnimationInstances) {
            if (!subInst.isSelected) continue;

            bool isParent =
                !(subInst.pFront || subInst.pBack || subInst.pLeft || subInst.pRight || subInst.pFrontRight ||
                  subInst.pFrontLeft || subInst.pBackRight || subInst.pBackLeft || subInst.pRandom || subInst.pDodge);

            if (isParent) {
                parentCounter++;
                if (parentCounter == movesetIndex) {
                    // Encontramos! Retorna o nome do MOD PAI (o moveset).
                    const auto& sourceMod = _allMods[modInst.sourceModIndex];
                    return sourceMod.name;
                }
            }
        }
    }

    return "Não encontrado";  // Se o índice estiver fora do alcance
}

// Adicione estas duas funções em Hooks.cpp

void AnimationManager::SaveStanceNames() {
    SKSE::log::info("Salvando nomes das stances...");
    const std::filesystem::path savePath = "Data/SKSE/Plugins/CycleMovesets/StanceNames.json";

    rapidjson::Document doc;
    doc.SetObject();
    auto& allocator = doc.GetAllocator();

    for (const auto& pair : _categories) {
        const WeaponCategory& category = pair.second;
        rapidjson::Value stanceNamesArray(rapidjson::kArrayType);
        for (const auto& name : category.stanceNames) {
            stanceNamesArray.PushBack(rapidjson::Value(name.c_str(), allocator), allocator);
        }
        doc.AddMember(rapidjson::Value(category.name.c_str(), allocator), stanceNamesArray, allocator);
    }

    // Escreve o arquivo
    std::ofstream ofs(savePath);
    if (!ofs) {
        SKSE::log::error("Falha ao abrir {} para escrita!", savePath.string());
        return;
    }
    rapidjson::StringBuffer buffer;
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    doc.Accept(writer);
    ofs << buffer.GetString();
    ofs.close();
    SKSE::log::info("Nomes das stances salvos com sucesso.");
}

void AnimationManager::LoadStanceNames() {
    SKSE::log::info("Carregando nomes das stances...");
    const std::filesystem::path loadPath = "Data/SKSE/Plugins/CycleMovesets/StanceNames.json";

    if (!std::filesystem::exists(loadPath)) {
        SKSE::log::info("Arquivo de nomes de stance não encontrado. Usando padrões.");
        return;
    }

    std::ifstream ifs(loadPath);
    if (!ifs) {
        SKSE::log::error("Falha ao abrir {} para leitura!", loadPath.string());
        return;
    }

    std::string jsonContent((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    rapidjson::Document doc;
    doc.Parse(jsonContent.c_str());

    if (doc.HasParseError() || !doc.IsObject()) {
        SKSE::log::error("Erro no parse do JSON de nomes de stance.");
        return;
    }

    for (auto& pair : _categories) {
        WeaponCategory& category = pair.second;
        if (doc.HasMember(category.name.c_str()) && doc[category.name.c_str()].IsArray()) {
            const auto& stanceNamesArray = doc[category.name.c_str()].GetArray();
            for (rapidjson::SizeType i = 0; i < stanceNamesArray.Size() && i < 4; ++i) {
                if (stanceNamesArray[i].IsString()) {
                    category.stanceNames[i] = stanceNamesArray[i].GetString();
                    // Atualiza o buffer do ImGui também
                    strcpy_s(category.stanceNameBuffers[i].data(), category.stanceNameBuffers[i].size(),
                             category.stanceNames[i].c_str());
                }
            }
        }
    }
    SKSE::log::info("Nomes de stance carregados com sucesso.");
}

void AnimationManager::DrawStanceEditorPopup() {
    if (_isEditStanceModalOpen) {
        ImGui::OpenPopup(LOC("edit_stance_name_popup"));
        _isEditStanceModalOpen = false;  // Reseta o gatilho
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal(LOC("edit_stance_name_popup"), NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(LOC("enter_new_stance_name"));
        ImGui::Separator();

        ImGui::PushItemWidth(300);
        ImGui::InputText("##NewStanceName", _editStanceNameBuffer, sizeof(_editStanceNameBuffer));
        ImGui::PopItemWidth();

        if (ImGui::Button(LOC("save"), ImVec2(120, 0))) {
            if (_categoryToEdit && _stanceIndexToEdit != -1) {
                // Salva o nome do buffer temporário para a estrutura de dados real
                _categoryToEdit->stanceNames[_stanceIndexToEdit] = _editStanceNameBuffer;
                // E também para o buffer que a Tab usa, para atualização visual instantânea
                strcpy_s(_categoryToEdit->stanceNameBuffers[_stanceIndexToEdit].data(),
                         _categoryToEdit->stanceNameBuffers[_stanceIndexToEdit].size(), _editStanceNameBuffer);
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(LOC("cancel"), ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void AnimationManager::DrawRestartPopup() {
    // Se a flag for verdadeira, nós dizemos ao ImGui para abrir o popup na próxima frame
    if (_showRestartPopup) {
        ImGui::OpenPopup("Restart Required");
        _showRestartPopup = false;  // Reseta a flag para não abrir toda hora
    }

    // Configura a posição central do popup
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    // Define o conteúdo do popup
    if (ImGui::BeginPopupModal("Restart Required", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Configs saved, reload the game to take effect.");
        ImGui::Separator();

        // Centraliza o botão OK
        float window_width = ImGui::GetWindowWidth();
        float button_width = 120.0f;
        ImGui::SetCursorPosX((window_width - button_width) * 0.5f);

        if (ImGui::Button("OK", ImVec2(button_width, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}