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
#include "Serialization.h"

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

    // >>> INÍCIO DA ADIÇÃO 1: LÓGICA DE CONVERSÃO PARA BFCO <<<
    bool shouldConvertToBFCO = false;
    if (doc.HasMember("convertBFCO") && doc["convertBFCO"].IsBool()) {
        shouldConvertToBFCO = doc["convertBFCO"].GetBool();
    }

    if (shouldConvertToBFCO && filesCopied > 0) {
        SKSE::log::info("Iniciando conversão de MCO para BFCO...");
        int filesRenamed = 0;
        for (const auto& fileEntry : std::filesystem::directory_iterator(destinationPath)) {
            if (fileEntry.is_regular_file()) {
                std::string filename = fileEntry.path().filename().string();
                std::string lowerFilename = filename;
                std::transform(lowerFilename.begin(), lowerFilename.end(), lowerFilename.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                if (lowerFilename.find("mco_") != std::string::npos) {
                    std::string newFilename = filename;
                    // Substitui a primeira ocorrência de "mco_" por "BFCO_"
                    size_t pos = newFilename.find("mco_");
                    if (pos != std::string::npos) {
                        newFilename.replace(pos, 4, "BFCO_");

                        std::filesystem::path newFilePath = destinationPath / newFilename;
                        try {
                            std::filesystem::rename(fileEntry.path(), newFilePath);
                            filesRenamed++;
                        } catch (const std::filesystem::filesystem_error& e) {
                            SKSE::log::error("Falha ao renomear {} para {}. Erro: {}", fileEntry.path().string(),
                                             newFilePath.string(), e.what());
                        }
                    }
                }
            }
        }
        SKSE::log::info("Conversão BFCO concluída. {} arquivos renomeados.", filesRenamed);
    }

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
    int hkxFileCount = 0;

   // Itera sobre todos os arquivos na pasta para encontrar tags de animação
    for (const auto& fileEntry : std::filesystem::directory_iterator(subAnimPath)) {
        if (fileEntry.is_regular_file()) {
            // --- PONTO 3: Ler apenas arquivos .hkx ---
            std::string extension = fileEntry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);

            // A lógica de verificação de tags agora só é executada para arquivos .hkx
            if (extension == ".hkx") {
                hkxFileCount++;
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
    // Se encontramos pelo menos um arquivo .hkx, marcamos a flag como verdadeira.
    if (hkxFileCount > 0) {
        subAnimDef.hasAnimations = true;
    }
    SKSE::log::info("Scan da pasta '{}': {} arquivos .hkx encontrados. hasAnimations = {}", subAnimDef.name,
                    hkxFileCount, subAnimDef.hasAnimations);
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
        double leftHandTypeValue = -1.0;
        bool isDual;
        std::vector<std::string> keywords;
        std::vector<std::string> leftHandKeywords;
    };

    std::vector<CategoryDefinition> categoryDefinitions = {
        {"Swords", 1.0, 0.0, false, {}, {}},
        {"Sword & Shield", 1.0, 11.0, false, {}, {}},
        {"Daggers", 2.0, 0.0, false, {}, {}},
        {"War Axes", 3.0, 0.0, false, {}, {}},
        {"Maces", 4.0, 0.0, false, {}, {}},
        {"Greatswords", 5.0, -1.0, false, {}, {}},

        {"Battleaxes", 6.0, -1.0, false, {}, {}},
        {"Warhammers", 10.0, -1.0, false, {}, {}},

        {"Katanas", 1.0, 0.0, false, {"OCF_WeapTypeKatana1H", "WeapTypeKatana"}, {}},
        {"Nodachi", 5.0, -1.0, false, {"OCF_WeapTypeKatana1H", "WeapTypeNodachi"}, {}},
        {"Claws", 2.0, 0.0, false, {"OCF_WeapTypeClaws1H", "WeapTypeClaw"}, {}},
        {"Pike", 5.0, -1.0, false, {"OCF_WeapTypePike2H", "WeapTypePike"}, {}},
        {"Twinblade", 5.0, -1.0, false, {"OCF_WeapTypeTwinblade2H", "WeapTypeTwinblade"}, {}},
        {"Halberd", 6.0, -1.0, false, {"OCF_WeapTypeHalberd2H", "WeapTypeHalberd"}, {}},
        {"Quarterstaff", 10.0, -1.0, false, {"WeapTypeQtrStaff", "WeapTypeQuarterstaff"}, {}},
        {"Rapier", 1.0, 0.0, false, {"OCF_WeapTypeRapier1H", "WeapTypeRapier"}, {}},
        {"Whip", 4.0, 0.0, false, {"OCF_WeapTypeWhip1H", "WeapTypeWhip"}, {}},
        {"Javelin", 4.0, 0.0, false, {"WeapTypeJavelin"}, {}},
        {"Scythe", 4.0, 0.0, false, {"WeapTypeScythe"}, {}},
        {"Spear", 4.0, 0.0, false, {"WeapTypeSpear"}, {}},

        // CATEGORIAS DUAL WIELD

        {"Dual Swords", 1.0, 1.0, true, {}, {}},
        {"Dual Daggers", 2.0, 2.0, true, {}, {}},
        {"Dual War Axes", 3.0, 3.0, true, {}, {}},
        {"Dual Maces", 4.0, 4.0, true, {}, {}},
        {"Dual Katanas",1.0,1.0,true,{"OCF_WeapTypeKatana1H", "WeapTypeKatana"},{"OCF_WeapTypeKatana1H", "WeapTypeKatana"}},
        {"Dual Rapier",1.0,1.0,true,{"OCF_WeapTypeRapier1H", "WeapTypeRapier"},{"OCF_WeapTypeRapier1H", "WeapTypeRapier"}},
        {"Dual Claws",2.0,2.0,true,{"OCF_WeapTypeClaws1H", "WeapTypeClaw"},{"OCF_WeapTypeClaws1H", "WeapTypeClaw"}},
        {"Unarmed", 0.0, 0.0, true, {}, {}}};

    //{"Bows", 7.0, false, {}},
    /*{"Alteration Spell", 12.0, false, {"MagicAlteration"}},
    {"Illusion Spell", 13.0, false, {"MagicIllusion"}},
    {"Destruction Spell", 14.0, false, {"MagicDestruction"}},
    {"Magic", 14.0, false, {}},
    {"Conjuration Spell", 15.0, false, {"MagicConjuration"}},
    {"Restoration Spell", 16.0, false, {"MagicRestoration"}},*/
    /*{"Alteration Spells", 12.0, true, {"MagicAlteration"}},
    {"Illusion Spells", 13.0, true, {"MagicIllusion"}},
    {"Destruction Spells", 14.0, true, {"MagicDestruction"}},
    {"Conjuration Spells", 15.0, true, {"MagicConjuration"}},
    {"Restoration Spells", 16.0, true, {"MagicRestoration"}},*/



    for (const auto& def : categoryDefinitions) {
        _categories[def.name].name = def.name;
        _categories[def.name].equippedTypeValue = def.typeValue;
        _categories[def.name].leftHandEquippedTypeValue = def.leftHandTypeValue;
        _categories[def.name].isDualWield = def.isDual;
        _categories[def.name].keywords = def.keywords;
        _categories[def.name].leftHandKeywords = def.leftHandKeywords;

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
        } else if (_modInstanceToAddTo || _userMovesetToAddTo || _stanceToAddTo) {
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
        ImGui::Text(LOC("library"));
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
        ImGui::Text(LOC("library"));
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

                                if (ImGui::Button(LOC("add_moveset"), ImVec2(button_width, 0))) {
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
                                    } else if (_stanceToAddTo) {
                                        //const auto& subAnimDef = modDef.subAnimations[subAnimIdx];
                                        // Cria uma instância completa da nova estrutura
                                        CreatorSubAnimationInstance newInstance;
                                        newInstance.sourceDef = &subAnimDef;
                                        strcpy_s(newInstance.editedName.data(), newInstance.editedName.size(),
                                                 subAnimDef.name.c_str());

                                        // Adiciona a nova instância completa ao vetor
                                        _stanceToAddTo->subMovesets.push_back(newInstance);
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
        if (ImGui::BeginTabItem(LOC("tab_moveset_creator"))) {
            DrawUserMovesetCreator();  // Chama a nova UI
            ImGui::EndTabItem();
        }
        //if (ImGui::BeginTabItem(LOC("tab_user_movesets"))) {
        //    DrawUserMovesetManager();  // Chama a UI da segunda aba
        //    ImGui::EndTabItem();
        //}
        ImGui::EndTabBar();
    }

    // CORREÇÃO: Chamamos a função do modal aqui, fora de qualquer aba.
    // Ele só será desenhado quando a flag _isAddModModalOpen for verdadeira,
    // mas agora ele não pertence a nenhuma aba específica.
    DrawAddModModal();
    DrawAddDarModal();
    DrawStanceEditorPopup();
    DrawRestartPopup();
}
// Nova função para desenhar a interface de criação de movesets
void AnimationManager::DrawUserMovesetCreator() {
    ImGui::Text("Ferramenta de Criação de Moveset");
    ImGui::Separator();

    // Seção de botões principais
    if (ImGui::Button("Salvar Novo Moveset")) {
        SaveUserMoveset();  // Função que vamos criar
    }
    ImGui::SameLine();
    if (ImGui::Button("Carregar Moveset para Editar")) {
        // LoadUserMoveset(); // Função para o requisito 2.5
    }
    ImGui::SameLine();
    if (ImGui::Button("Ler Animações DAR")) {
        ScanDarAnimations();
    }
    ImGui::Separator();

    // Campos de Informação do Moveset
    ImGui::InputText("Nome do Moveset", _newMovesetName, sizeof(_newMovesetName));
    ImGui::InputText("Autor", _newMovesetAuthor, sizeof(_newMovesetAuthor));
    ImGui::InputText("Descrição", _newMovesetDesc, sizeof(_newMovesetDesc));

    // Seleção de Categoria de Arma
    std::vector<const char*> categoryNames;
    for (const auto& pair : _categories) {
        categoryNames.push_back(pair.first.c_str());
    }
    ImGui::Combo("Categoria de Arma", &_newMovesetCategoryIndex, categoryNames.data(), categoryNames.size());

    // Seleção de Tipo (MCO/BFCO)
    ImGui::RadioButton("MCO", reinterpret_cast<int*>(&_newMovesetIsBFCO), 0);
    ImGui::SameLine();
    ImGui::RadioButton("BFCO", reinterpret_cast<int*>(&_newMovesetIsBFCO), 1);

    ImGui::Separator();
    ImGui::Text("Stances e Sub-Movesets");

    // Abas para as 4 Stances
    if (ImGui::BeginTabBar("NewMovesetStanceTabs")) {
        for (int i = 0; i < 4; ++i) {
            std::string stanceTabName = std::format("Stance {}", i + 1);
            if (ImGui::BeginTabItem(stanceTabName.c_str(), &_newMovesetStanceEnabled[i])) {
                if (ImGui::Button(std::format("Adicionar Sub-Moveset à Stance {}", i + 1).c_str())) {
                    _isAddModModalOpen = true;
                    _stanceToAddTo = &_newMovesetStances[i];
                    _instanceToAddTo = nullptr;
                    _modInstanceToAddTo = nullptr;
                    _userMovesetToAddTo = nullptr;
                }
                ImGui::SameLine();
                if (ImGui::Button(std::format("Adicionar Animação (DAR) à Stance {}", i + 1).c_str())) {
                    _isAddDarModalOpen = true;  // Ativa o novo modal
                    _stanceToAddTo = &_newMovesetStances[i];
                    _instanceToAddTo = nullptr;
                    _modInstanceToAddTo = nullptr;
                    _userMovesetToAddTo = nullptr;
                }
                ImGui::Separator();

                int subToRemove = -1;
                for (size_t j = 0; j < _newMovesetStances[i].subMovesets.size(); ++j) {
                    auto& subInst = _newMovesetStances[i].subMovesets[j];

                    ImGui::PushID(static_cast<int>(j));
                    if (ImGui::Button("X")) {
                        subToRemove = j;
                    }
                    ImGui::SameLine();
                    ImGui::InputText("##SubName", subInst.editedName.data(), subInst.editedName.size());
                    ImGui::SameLine();
                    ImGui::Text("<- %s", subInst.sourceDef->name.c_str());

                    // ======================= ADIÇÃO DAS CHECKBOXES (Problema 3) =======================
                    ImGui::Indent();  // Adiciona um recuo para as checkboxes

                    ImGui::Checkbox("F", &subInst.pFront);
                    ImGui::SameLine();
                    ImGui::Checkbox("B", &subInst.pBack);
                    ImGui::SameLine();
                    ImGui::Checkbox("L", &subInst.pLeft);
                    ImGui::SameLine();
                    ImGui::Checkbox("R", &subInst.pRight);
                    ImGui::SameLine();
                    ImGui::Checkbox("FR", &subInst.pFrontRight);
                    ImGui::SameLine();
                    ImGui::Checkbox("FL", &subInst.pFrontLeft);
                    ImGui::SameLine();
                    ImGui::Checkbox("BR", &subInst.pBackRight);
                    ImGui::SameLine();
                    ImGui::Checkbox("BL", &subInst.pBackLeft);
                    ImGui::SameLine();
                    ImGui::Checkbox("Rnd", &subInst.pRandom);
                    ImGui::SameLine();
                    ImGui::Checkbox("Movement", &subInst.pDodge);

                    ImGui::Unindent();
                    // ===============================================================================

                    ImGui::PopID();
                    ImGui::Separator();
                }

                if (subToRemove != -1) {
                    _newMovesetStances[i].subMovesets.erase(_newMovesetStances[i].subMovesets.begin() + subToRemove);
                }

                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
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
                const char* currentStanceName = category.stanceNameBuffers[i].data();
                bool tab_open = ImGui::BeginTabItem(currentStanceName);
                if (tab_open) {
                    category.activeInstanceIndex = i;
                    CategoryInstance& instance = category.instances[i];

                    std::map<SubAnimationInstance*, int> playlistNumbers;
                    std::map<SubAnimationInstance*, int> parentNumbersForChildren;
                    int currentPlaylistCounter = 1;
                    int lastValidParentNumber = 0;
                    for (auto& modInst : instance.modInstances) {
                        if (!modInst.isSelected) continue;
                        for (auto& subInst : modInst.subAnimationInstances) {
                            if (!subInst.isSelected) continue;
                            bool isParent = !(subInst.pFront || subInst.pBack || subInst.pLeft || subInst.pRight ||
                                              subInst.pFrontRight || subInst.pFrontLeft || subInst.pBackRight ||
                                              subInst.pBackLeft || subInst.pRandom || subInst.pDodge);
                            if (isParent) {
                                lastValidParentNumber = currentPlaylistCounter;
                                playlistNumbers[&subInst] = currentPlaylistCounter;
                                currentPlaylistCounter++;
                            } else {
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
                    ImGui::SameLine();
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

                            for (size_t sub_j = 0; sub_j < modInstance.subAnimationInstances.size(); ++sub_j) {
                                auto& subInstance = modInstance.subAnimationInstances[sub_j];
                                const auto& originMod = _allMods[subInstance.sourceModIndex];
                                const auto& originSubAnim = originMod.subAnimations[subInstance.sourceSubAnimIndex];

                                ImGui::PushID(static_cast<int>(sub_j));
                                const bool isChildDisabled = !subInstance.isSelected || isParentDisabled;
                                if (isChildDisabled) {
                                    ImGui::PushStyleColor(ImGuiCol_Text,
                                                          ImGui::GetStyle()->Colors[ImGuiCol_TextDisabled]);
                                }

                                ImGui::Separator();

                                // --- Coluna 1 (Info) ---
                                ImGui::BeginGroup();
                                ImGui::Checkbox("##subselect", &subInstance.isSelected);
                                ImGui::SameLine();

                                ImGui::BeginGroup();

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

                                // ✅ **CORREÇÃO APLICADA AQUI**
                                ImVec2 selectableSize;
                                ImVec2 contentRegionAvail;
                                ImGui::GetContentRegionAvail(&contentRegionAvail);
                                selectableSize.x = contentRegionAvail.x * 0.5f;  // Metade do espaço restante
                                selectableSize.y = ImGui::GetTextLineHeight();
                                ImGui::Selectable(label.c_str(), false, 0, selectableSize);

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

                                bool firstTag = true;
                                if (originSubAnim.attackCount > 0) {
                                    if (!firstTag) ImGui::SameLine();
                                    ImGui::TextColored({1.0f, 0.4f, 0.4f, 1.0f}, "[HitCombo: %d]",
                                                       originSubAnim.attackCount);
                                    firstTag = false;
                                }
                                if (originSubAnim.powerAttackCount > 0) {
                                    if (!firstTag) ImGui::SameLine();
                                    ImGui::TextColored({1.0f, 0.6f, 0.2f, 1.0f}, "[PA: %d]",
                                                       originSubAnim.powerAttackCount);
                                    firstTag = false;
                                }
                                if (originSubAnim.hasIdle) {
                                    if (!firstTag) ImGui::SameLine();
                                    ImGui::TextColored({0.4f, 0.6f, 1.0f, 1.0f}, "[Idle]");
                                    firstTag = false;
                                }

                                ImGui::EndGroup();
                                ImGui::EndGroup();

                                ImGui::SameLine();

                                // --- Coluna 2 (Checkboxes) ---
                                ImGui::BeginGroup();

                                struct CheckboxInfo {
                                    const char* label;
                                    bool* value;
                                };
                                std::vector<CheckboxInfo> checkboxes = {
                                    {"F", &subInstance.pFront},       {"B", &subInstance.pBack},
                                    {"L", &subInstance.pLeft},        {"R", &subInstance.pRight},
                                    {"FR", &subInstance.pFrontRight}, {"FL", &subInstance.pFrontLeft},
                                    {"BR", &subInstance.pBackRight},  {"BL", &subInstance.pBackLeft},
                                    {"Rnd", &subInstance.pRandom},    {"Movement", &subInstance.pDodge}};

                                ImGui::GetContentRegionAvail(&contentRegionAvail);
                                float availableWidth = contentRegionAvail.x;
                                float currentX = 0.0f;
                                float itemSpacing = ImGui::GetStyle()->ItemSpacing.x;
                                float itemInnerSpacing = ImGui::GetStyle()->ItemInnerSpacing.x;

                                for (size_t k = 0; k < checkboxes.size(); ++k) {
                                    const auto& cb = checkboxes[k];

                                    ImVec2 textSize;
                                    ImGui::CalcTextSize(&textSize, cb.label, NULL, false, 0.0f);

                                    float checkboxWidth = ImGui::GetFrameHeight() + itemInnerSpacing + textSize.x;

                                    if (k > 0) {
                                        if (currentX > 0.0f &&
                                            (currentX + itemSpacing + checkboxWidth) > availableWidth) {
                                            currentX = 0.0f;
                                        } else {
                                            ImGui::SameLine();
                                            currentX += itemSpacing;
                                        }
                                    }

                                    ImGui::Checkbox(cb.label, cb.value);
                                    currentX += checkboxWidth;
                                }

                                ImGui::EndGroup();

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

    //RecarregarAnimacoesOAR();
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
    std::string movesetName = jsonPath.parent_path().filename().string();
    if (doc.HasMember("name")) {
        doc["name"].SetString(movesetName.c_str(), allocator);
    } else {
        // Adiciona o novo membro. A ordem exata não é garantida, mas não é necessária.
        doc.AddMember("name", rapidjson::Value(movesetName.c_str(), allocator), allocator);
    }
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

            if (!config.category->leftHandKeywords.empty()) {
                // Adiciona as condições de keyword requeridas para a mão esquerda
                AddKeywordOrConditions(andConditions, config.category->leftHandKeywords, true,
                                       allocator);  // 'true' para Left Hand

                // Adiciona as exclusões de keywords concorrentes para a mão esquerda
                AddCompetingKeywordExclusions(andConditions, config.category, true,
                                              allocator);  // 'true' para Left Hand
            }
            // NOVA LÓGICA PARA MÃO ESQUERDA (DUAL WIELD vs UMA MÃO)
            if (config.category->leftHandEquippedTypeValue >= 0.0) {
                rapidjson::Value equippedTypeL(rapidjson::kObjectType);
                equippedTypeL.AddMember("condition", "IsEquippedType", allocator);
                rapidjson::Value typeValL(rapidjson::kObjectType);
                typeValL.AddMember("value", config.category->leftHandEquippedTypeValue,
                                    allocator);  // Usa o novo valor
                equippedTypeL.AddMember("Type", typeValL, allocator);
                equippedTypeL.AddMember("Left hand", true, allocator);
                andConditions.PushBack(equippedTypeL, allocator);
                   // Se a categoria for dual wield do mesmo tipo, também verifica keywords na mão esquerda.
                  if (config.category->isDualWield &&
                       config.category->equippedTypeValue == config.category->leftHandEquippedTypeValue) {
                    AddKeywordOrConditions(andConditions, config.category->keywords, true,
                                            allocator);  // 'true' para Left Hand
                    AddCompetingKeywordExclusions(andConditions, config.category, true,
                                                   allocator);  // 'true' para Left Hand
                    
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
                    // --- INÍCIO DA NOVA VERIFICAÇÃO ---
                    // Pega a definição original da sub-animação para verificar a flag
                    const auto& sourceSubAnim =
                        _allMods[subInst.sourceModIndex].subAnimations[subInst.sourceSubAnimIndex];
                    if (!sourceSubAnim.hasAnimations) {
                        continue;  // PULA este sub-moveset se ele não tiver animações .hkx
                    }
                    // --- FIM DA NOVA VERIFICAÇÃO ---
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
                    int animationIndexCounter = 1;
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
                        animObj.AddMember("index", animationIndexCounter++, allocator);
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
// SUBSTITUA a função inteira por esta versão corrigida
// SUBSTITUA a função inteira por esta versão corrigida e aprimorada
void AnimationManager::LoadCycleMovesets() {
    SKSE::log::info("Iniciando carregamento descentralizado de arquivos CycleMoveset.json...");

    // Limpa as instâncias existentes antes de carregar
    for (auto& pair : _categories) {
        for (auto& instance : pair.second.instances) instance.modInstances.clear();
    }
    for (auto& pair : _npcCategories) {
        for (auto& instance : pair.second.instances) instance.modInstances.clear();
    }

    // >>> CORREÇÃO: Buffer temporário para coletar e ordenar os sub-movesets <<<
    // A chave externa é o ModInstance ao qual os sub-movesets pertencem.
    // A chave interna (no std::map) é o 'index' do JSON, que ordena automaticamente as entradas.
    std::map<ModInstance*, std::map<int, SubAnimationInstance>> loadingBuffer;

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
                                for (const auto& animJson : stanceJson["animations"].GetArray()) {
                                    if (!animJson.IsObject() || !animJson.HasMember("index")) continue;

                                    // >>> CORREÇÃO: Ler o índice do JSON <<<
                                    int animIndex = animJson["index"].GetInt();

                                    SubAnimationInstance newSubInstance;

                                    std::string subModName = animJson["sourceModName"].GetString();
                                    std::string subAnimName = animJson["sourceSubName"].GetString();

                                    auto subModIdxOpt = FindModIndexByName(subModName);
                                    if (subModIdxOpt) {
                                        newSubInstance.sourceModIndex = *subModIdxOpt;
                                        auto subAnimIdxOpt = FindSubAnimIndexByName(*subModIdxOpt, subAnimName);
                                        if (subAnimIdxOpt) {
                                            newSubInstance.sourceSubAnimIndex = *subAnimIdxOpt;
                                        } else {
                                            SKSE::log::warn("Sub-moveset '{}' não encontrado no mod '{}'. Pulando.",
                                                            subAnimName, subModName);
                                            continue;
                                        }
                                    } else {
                                        SKSE::log::warn("Mod '{}' para o sub-moveset não encontrado. Pulando.",
                                                        subModName);
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

                                    // >>> CORREÇÃO: Armazenar no buffer em vez de adicionar diretamente <<<
                                    loadingBuffer[modInstancePtr][animIndex] = newSubInstance;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // >>> CORREÇÃO: Após ler todos os arquivos, popular os vetores finais a partir do buffer ordenado <<<
    for (auto const& [modInstance, sortedAnims] : loadingBuffer) {
        // Limpa o vetor para garantir que não haja duplicatas de execuções anteriores
        modInstance->subAnimationInstances.clear();
        for (auto const& [index, subInstance] : sortedAnims) {
            modInstance->subAnimationInstances.push_back(subInstance);
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
std::string AnimationManager::GetCurrentMovesetName(const std::string& categoryName, int stanceIndex, int movesetIndex,
                                                    int directionalState) {

    // <--- LOG 1: Entrada da Função ---
    SKSE::log::info("[GetCurrentMovesetName] Buscando: Cat='{}', Stance={}, MovesetIdx={}, DirState={}", categoryName,
                    stanceIndex, movesetIndex, directionalState);

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
    // --- PASSO 1: Encontrar o nome do PAI (fallback) ---
    std::string parentSubmovesetName = "Não encontrado";
    int parentCounter = 0;
    for (auto& modInst : instance.modInstances) {
        if (!modInst.isSelected) continue;
        for (auto& subInst : modInst.subAnimationInstances) {
            if (!subInst.isSelected) continue;
            // --- INÍCIO DA NOVA VERIFICAÇÃO (idêntica à anterior) ---
            const auto& sourceSubAnim = _allMods[subInst.sourceModIndex].subAnimations[subInst.sourceSubAnimIndex];
            if (!sourceSubAnim.hasAnimations) {
                continue;  // PULA este sub-moveset se ele não tiver animações .hkx
            }
            // --- FIM DA NOVA VERIFICAÇÃO ---
            bool isParent =
                !(subInst.pFront || subInst.pBack || subInst.pLeft || subInst.pRight || subInst.pFrontRight ||
                  subInst.pFrontLeft || subInst.pBackRight || subInst.pBackLeft || subInst.pRandom || subInst.pDodge);

            if (isParent) {
                parentCounter++;
                if (parentCounter == movesetIndex) {
                    const auto& sourceSubAnimParent =
                        _allMods[subInst.sourceModIndex].subAnimations[subInst.sourceSubAnimIndex];
                    parentSubmovesetName = sourceSubAnimParent.name;
                    SKSE::log::info("  -> Pai #{} encontrado: '{}'.", movesetIndex, parentSubmovesetName);
                    goto found_parent;  // Pula para fora dos loops aninhados
                }
            }
        }
    }

found_parent:
    // Se não encontrou o pai ou não há direção, retorna o que encontrou até agora.
    if (parentSubmovesetName == "Não encontrado" || directionalState == 0) {
        if (parentSubmovesetName == "Não encontrado") {
            SKSE::log::warn("[GetCurrentMovesetName] Nenhum moveset encontrado para o índice {}", movesetIndex);
        }
        return parentSubmovesetName;
    }

    // --- PASSO 2: Procurar por um FILHO DIRECIONAL em TODOS os pacotes de moveset da stance ---
    SKSE::log::info("  -> Procurando por filho direcional em todos os pacotes...");
    for (auto& modInst : instance.modInstances) {
        if (!modInst.isSelected) continue;
        for (auto& childSubInst : modInst.subAnimationInstances) {
            if (!childSubInst.isSelected) continue;

            // Log de verificação (pode ser comentado depois de funcionar)
            SKSE::log::info("    -- Verificando filho: '{}' [F:{}, B:{}, L:{}, R:{}, FR:{}, FL:{}, BR:{}, BL:{}]",
                            _allMods[childSubInst.sourceModIndex].subAnimations[childSubInst.sourceSubAnimIndex].name,
                            childSubInst.pFront, childSubInst.pBack, childSubInst.pLeft, childSubInst.pRight,
                            childSubInst.pFrontRight, childSubInst.pFrontLeft, childSubInst.pBackRight,
                            childSubInst.pBackLeft);

            bool isDirectionalMatch =
                (directionalState == 1 && childSubInst.pFront) || (directionalState == 2 && childSubInst.pFrontRight) ||
                (directionalState == 3 && childSubInst.pRight) || (directionalState == 4 && childSubInst.pBackRight) ||
                (directionalState == 5 && childSubInst.pBack) || (directionalState == 6 && childSubInst.pBackLeft) ||
                (directionalState == 7 && childSubInst.pLeft) || (directionalState == 8 && childSubInst.pFrontLeft);

            if (isDirectionalMatch) {
                const auto& sourceSubAnimChild =
                    _allMods[childSubInst.sourceModIndex].subAnimations[childSubInst.sourceSubAnimIndex];
                SKSE::log::info("      ==> MATCH ENCONTRADO! Retornando nome do filho: {}", sourceSubAnimChild.name);
                return sourceSubAnimChild.name;
            }
        }
    }

    // Se nenhum filho foi encontrado em nenhum pacote, retorna o nome do pai
    SKSE::log::info("  -> Nenhum filho direcional correspondeu em nenhum pacote. Retornando nome do pai: {}",
                    parentSubmovesetName);
    return parentSubmovesetName;
}

// Adicione estas duas funções em Hooks.cpp

void AnimationManager::SaveStanceNames() {
    SKSE::log::info("Salvando nomes das stances em arquivos separados por categoria...");
    const std::filesystem::path stancesFolderPath = "Data/SKSE/Plugins/CycleMovesets/Stances";

    try {
        // Garante que o diretório "Stances" existe
        if (!std::filesystem::exists(stancesFolderPath)) {
            std::filesystem::create_directories(stancesFolderPath);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        SKSE::log::error("Falha ao criar o diretório de stances: {}. Erro: {}", stancesFolderPath.string(), e.what());
        return;
    }

    // Itera sobre cada categoria de arma
    for (const auto& pair : _categories) {
        const WeaponCategory& category = pair.second;
        std::filesystem::path categorySavePath = stancesFolderPath / (category.name + ".json");

        rapidjson::Document doc;
        doc.SetArray();  // O documento raiz será um array
        auto& allocator = doc.GetAllocator();

        // Adiciona os 4 nomes de stance ao array
        for (const auto& name : category.stanceNames) {
            doc.PushBack(rapidjson::Value(name.c_str(), allocator), allocator);
        }

        // Escreve o arquivo JSON específico para esta categoria
        std::ofstream ofs(categorySavePath);
        if (!ofs) {
            SKSE::log::error("Falha ao abrir {} para escrita!", categorySavePath.string());
            continue;  // Pula para a próxima categoria em caso de erro
        }

        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        ofs << buffer.GetString();
        ofs.close();
    }

    SKSE::log::info("Nomes das stances salvos com sucesso em arquivos individuais.");
}

void AnimationManager::LoadStanceNames() {
    SKSE::log::info("Carregando nomes das stances de arquivos individuais por categoria...");
    const std::filesystem::path stancesFolderPath = "Data/SKSE/Plugins/CycleMovesets/Stances";

    if (!std::filesystem::exists(stancesFolderPath)) {
        SKSE::log::info("Diretório de nomes de stance não encontrado. Usando padrões.");
        return;
    }

    // Itera sobre cada categoria de arma para carregar seu respectivo arquivo
    for (auto& pair : _categories) {
        WeaponCategory& category = pair.second;
        std::filesystem::path categoryLoadPath = stancesFolderPath / (category.name + ".json");

        if (!std::filesystem::exists(categoryLoadPath)) {
            // Se o arquivo para esta categoria não existe, apenas pula para a próxima
            continue;
        }

        std::ifstream ifs(categoryLoadPath);
        if (!ifs) {
            SKSE::log::error("Falha ao abrir {} para leitura!", categoryLoadPath.string());
            continue;
        }

        std::string jsonContent((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        ifs.close();

        rapidjson::Document doc;
        doc.Parse(jsonContent.c_str());

        if (doc.HasParseError() || !doc.IsArray()) {
            SKSE::log::error("Erro no parse do JSON ou o arquivo não é um array para a categoria: {}", category.name);
            continue;
        }

        const auto& stanceNamesArray = doc.GetArray();
        for (rapidjson::SizeType i = 0; i < stanceNamesArray.Size() && i < 4; ++i) {
            if (stanceNamesArray[i].IsString()) {
                category.stanceNames[i] = stanceNamesArray[i].GetString();
                // Atualiza o buffer do ImGui também para a UI refletir a mudança
                strcpy_s(category.stanceNameBuffers[i].data(), category.stanceNameBuffers[i].size(),
                         category.stanceNames[i].c_str());
            }
        }
    }

    SKSE::log::info("Nomes de stance individuais carregados com sucesso.");
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

void AnimationManager::SaveUserMoveset() {
    std::string movesetName = _newMovesetName;
    if (movesetName.empty()) {
        SKSE::log::error("Não é possível salvar um moveset sem nome.");
        RE::DebugNotification("ERRO: O nome do moveset não pode estar vazio!");
        return;
    }

    SKSE::log::info("Iniciando salvamento do moveset do usuário: {}", movesetName);

    const std::filesystem::path oarRootPath = "Data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer";
    std::filesystem::path newMovesetPath = oarRootPath / movesetName;

    // 1. Criar pastas (lógica principal e do moveset) - sem alterações
    try {
        if (!std::filesystem::exists(newMovesetPath.parent_path())) {
            std::filesystem::create_directories(newMovesetPath.parent_path());
        }
        if (!std::filesystem::exists(newMovesetPath)) {
            std::filesystem::create_directory(newMovesetPath);
        }
    } catch (const std::filesystem::filesystem_error& e) {
        SKSE::log::error("Falha ao criar a pasta do moveset: {}. Erro: {}", newMovesetPath.string(), e.what());
        RE::DebugNotification("ERRO: Falha ao criar a pasta do moveset!");
        return;
    }

    // 2. Criar o config.json principal (sem alterações)
    {
        rapidjson::Document doc;
        doc.SetObject();
        auto& allocator = doc.GetAllocator();
        doc.AddMember("name", rapidjson::Value(_newMovesetName, allocator), allocator);
        doc.AddMember("author", rapidjson::Value(_newMovesetAuthor, allocator), allocator);
        doc.AddMember("description", rapidjson::Value(_newMovesetDesc, allocator), allocator);
        doc.AddMember("createdBy", "MyCustomTool", allocator);
        std::ofstream outFile(newMovesetPath / "config.json");
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        doc.Accept(writer);
        outFile << buffer.GetString();
        outFile.close();
    }

    // ======================= INÍCIO DA CORREÇÃO (Problema 1) =======================

    // 3. Coletar TODAS as configurações ANTES de salvar os arquivos
    std::map<std::filesystem::path, std::vector<FileSaveConfig>> fileUpdates;

    std::vector<std::string> categoryNames;
    for (const auto& pair : _categories) categoryNames.push_back(pair.first);
    auto categoryIt = _categories.find(categoryNames[_newMovesetCategoryIndex]);
    if (categoryIt == _categories.end()) {
        SKSE::log::error("Categoria selecionada inválida durante o salvamento.");
        return;
    }
    WeaponCategory* selectedCategory = &categoryIt->second;

    for (int i = 0; i < 4; ++i) {  // Loop das Stances
        if (!_newMovesetStanceEnabled[i]) continue;

        const auto& stance = _newMovesetStances[i];
        int playlistParentCounter = 1;
        int lastParentOrder = 0;

        for (const auto& subInst : stance.subMovesets) {  // Loop dos Sub-Movesets
            std::string subMovesetFolderName = subInst.editedName.data();
            if (subMovesetFolderName.empty()) continue;

            std::filesystem::path subMovesetPath = newMovesetPath / subMovesetFolderName;
            std::filesystem::path newConfigPath = subMovesetPath / "config.json";

            // Prepara a estrutura de dados
            FileSaveConfig config;
            config.isNPC = false;
            config.instance_index = i + 1;
            config.category = selectedCategory;

            // Copia o estado das checkboxes
            config.pFront = subInst.pFront;
            config.pBack = subInst.pBack;
            config.pLeft = subInst.pLeft;
            config.pRight = subInst.pRight;
            config.pFrontRight = subInst.pFrontRight;
            config.pFrontLeft = subInst.pFrontLeft;
            config.pBackRight = subInst.pBackRight;
            config.pBackLeft = subInst.pBackLeft;
            config.pRandom = subInst.pRandom;
            config.pDodge = subInst.pDodge;

            // Determina se é Pai ou Filho e a ordem
            config.isParent =
                !(config.pFront || config.pBack || config.pLeft || config.pRight || config.pFrontRight ||
                  config.pFrontLeft || config.pBackRight || config.pBackLeft || config.pRandom || config.pDodge);

            if (config.isParent) {
                lastParentOrder = playlistParentCounter;
                config.order_in_playlist = playlistParentCounter++;
            } else {
                config.order_in_playlist = lastParentOrder;
            }

            fileUpdates[newConfigPath].push_back(config);
            // 3.1. Criar a pasta do sub-moveset
            try {
                if (!std::filesystem::exists(subMovesetPath)) std::filesystem::create_directory(subMovesetPath);
            } catch (const std::filesystem::filesystem_error& e) {
                SKSE::log::error("Falha ao criar a pasta do sub-moveset: {}. Erro: {}", subMovesetPath.string(),
                                 e.what());
                continue;
            }
            
            // 3.2. Criar o CycleDar.json do sub-moveset
            {  
                rapidjson::Document cycleDoc;
                cycleDoc.SetObject();
                auto& allocator = cycleDoc.GetAllocator();
                std::string originalPathStr;
                // Se o 'path' do sourceDef aponta para um "config.json", é um sub-moveset OAR.
                if (subInst.sourceDef->path.filename() == "config.json") {
                    originalPathStr = subInst.sourceDef->path.parent_path().string();
                    SKSE::log::info("Gerando CycleDar.json para fonte OAR: {}", originalPathStr);
                } else {
                    // Caso contrário, é um sub-moveset DAR, e o 'path' já é a pasta correta.
                    originalPathStr = subInst.sourceDef->path.string();
                    SKSE::log::info("Gerando CycleDar.json para fonte DAR: {}", originalPathStr);
                }
                size_t pos = originalPathStr.find("Data\\");
                if (pos != std::string::npos) {
                    originalPathStr = originalPathStr.substr(pos + 5);
                }
                cycleDoc.AddMember("pathDar", rapidjson::Value(originalPathStr.c_str(), allocator), allocator);
                cycleDoc.AddMember("conversionDone", false, allocator);
                cycleDoc.AddMember("convertBFCO", _newMovesetIsBFCO, allocator);
                std::ofstream outFile(subMovesetPath / "CycleDar.json");
                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
                cycleDoc.Accept(writer);
                outFile << buffer.GetString();
                outFile.close();
            }
           
        }
    }
    // 4. Agora, iterar sobre o mapa e salvar cada config.json UMA VEZ com todas as suas condições
    for (const auto& pair : fileUpdates) {
        const auto& path = pair.first;
        const auto& configs = pair.second;
        UpdateOrCreateJson(path, configs);
    }
    SKSE::log::info("Salvamento do moveset '{}' concluído.", movesetName);
    RE::DebugNotification(std::format("Moveset '{}' salvo com sucesso!", movesetName).c_str());

}

// =================================================================================
// NOVA FUNÇÃO: Escaneia o diretório do DAR em busca de pastas de animação
// =================================================================================
// =================================================================================
// NOVA VERSÃO COM LOGS DETALHADOS PARA DEPURAÇÃO
// =================================================================================
void AnimationManager::ScanDarAnimations() {
    SKSE::log::info("[ScanDarAnimations] Iniciando a função de escaneamento DAR.");
    try {
        _darSubMovesets.clear();
        SKSE::log::info("[ScanDarAnimations] Vetor _darSubMovesets foi limpo.");

        const std::filesystem::path darRootPath =
            "Data\\meshes\\actors\\character\\animations\\DynamicAnimationReplacer\\_CustomConditions";

        // Convertendo para std::string para o log
        auto u8_darRootPath = darRootPath.u8string();
        SKSE::log::info("[ScanDarAnimations] Caminho a ser verificado: {}",
                        std::string(u8_darRootPath.begin(), u8_darRootPath.end()));

        if (!std::filesystem::exists(darRootPath) || !std::filesystem::is_directory(darRootPath)) {
            SKSE::log::warn("[ScanDarAnimations] A pasta raiz do DAR (_CustomConditions) não foi encontrada em '{}'.",
                            std::string(u8_darRootPath.begin(), u8_darRootPath.end()));
            RE::DebugNotification("Pasta do DAR (_CustomConditions) não encontrada.");
            return;
        }

        SKSE::log::info("[ScanDarAnimations] Pasta encontrada. Iniciando iteração pelas subpastas...");
        int folderCount = 0;
        for (const auto& entry : std::filesystem::directory_iterator(darRootPath)) {
            folderCount++;

            auto u8_entryPath = entry.path().u8string();
            SKSE::log::info("[ScanDarAnimations] [LOOP {}] Verificando a entrada: '{}'", folderCount,
                            std::string(u8_entryPath.begin(), u8_entryPath.end()));

            if (entry.is_directory()) {
                SKSE::log::info("[ScanDarAnimations] [LOOP {}] É um diretório. Processando...", folderCount);

                SubAnimationDef subAnimDef;

                SKSE::log::info("[ScanDarAnimations] [LOOP {}] Extraindo nome da pasta...", folderCount);

                // ===================================================================
                // CORREÇÃO PRINCIPAL: Converter explicitamente std::u8string para std::string
                // ===================================================================
                auto u8_filename = entry.path().filename().u8string();
                subAnimDef.name = std::string(u8_filename.begin(), u8_filename.end());
                SKSE::log::info("[ScanDarAnimations] [LOOP {}] Nome extraído: '{}'", folderCount, subAnimDef.name);

                subAnimDef.path = entry.path();
                auto u8_subAnimPath = subAnimDef.path.u8string();
                SKSE::log::info("[ScanDarAnimations] [LOOP {}] Path definido como: '{}'", folderCount,
                                std::string(u8_subAnimPath.begin(), u8_subAnimPath.end()));

                SKSE::log::info("[ScanDarAnimations] [LOOP {}] Chamando ScanSubAnimationFolderForTags para '{}'...",
                                folderCount, subAnimDef.name);
                ScanSubAnimationFolderForTags(entry.path(), subAnimDef);
                SKSE::log::info(
                    "[ScanDarAnimations] [LOOP {}] Retornou de ScanSubAnimationFolderForTags. A pasta tem animações: "
                    "{}",
                    folderCount, subAnimDef.hasAnimations);

                if (subAnimDef.hasAnimations) {
                    SKSE::log::info("[ScanDarAnimations] [LOOP {}] O submoveset tem animações. Adicionando ao vetor...",
                                    folderCount);
                    _darSubMovesets.push_back(subAnimDef);
                    SKSE::log::info("[ScanDarAnimations] [LOOP {}] Adicionado com sucesso: '{}'", folderCount,
                                    subAnimDef.name);
                } else {
                    SKSE::log::info(
                        "[ScanDarAnimations] [LOOP {}] O submoveset '{}' não contém arquivos .hkx e será pulado.",
                        folderCount, subAnimDef.name);
                }
            } else {
                SKSE::log::info("[ScanDarAnimations] [LOOP {}] A entrada não é um diretório. Pulando.", folderCount);
            }
        }
    } catch (const std::filesystem::filesystem_error& e) {
        SKSE::log::critical("[ScanDarAnimations] CRASH! ERRO DE FILESYSTEM DURANTE O SCAN: {}", e.what());
        RE::DebugNotification("ERRO GRAVE ao ler pastas DAR! Verifique os logs.");
    } catch (const std::exception& e) {
        SKSE::log::critical("[ScanDarAnimations] CRASH! ERRO GERAL DURANTE O SCAN: {}", e.what());
        RE::DebugNotification("ERRO GRAVE ao ler pastas DAR! Verifique os logs.");
    } catch (...) {
        SKSE::log::critical("[ScanDarAnimations] CRASH! ERRO DESCONHECIDO E NÃO IDENTIFICADO DURANTE O SCAN!");
        RE::DebugNotification("ERRO GRAVE E DESCONHECIDO ao ler pastas DAR! Verifique os logs.");
    }

    SKSE::log::info("[ScanDarAnimations] Escaneamento finalizado. Total de {} submovesets carregados.",
                    _darSubMovesets.size());
    if (!_darSubMovesets.empty()) {
        RE::DebugNotification(std::format("{} Animações DAR carregadas.", _darSubMovesets.size()).c_str());
    }
}

// =================================================================================
// NOVA FUNÇÃO: Desenha o modal para adicionar animações da biblioteca DAR
// =================================================================================
void AnimationManager::DrawAddDarModal() {
    if (_isAddDarModalOpen) {
        ImGui::OpenPopup("Adicionar Animação DAR");
        _isAddDarModalOpen = false;
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 modal_list_size = ImVec2(viewport->Size.x * 0.5f, viewport->Size.y * 0.5f);
    ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Adicionar Animação DAR", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Biblioteca de Animações DAR");
        ImGui::Separator();
        static char darFilter[128] = "";
        ImGui::InputText("Filtro", darFilter, sizeof(darFilter));
        ImGui::Separator();

        if (ImGui::BeginChild("BibliotecaDAR", ImVec2(modal_list_size), true)) {
            std::string filter_str = darFilter;
            std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

            for (size_t i = 0; i < _darSubMovesets.size(); ++i) {
                const auto& darSubDef = _darSubMovesets[i];
                std::string name_lower = darSubDef.name;
                std::transform(name_lower.begin(), name_lower.end(), name_lower.begin(), ::tolower);

                if (filter_str.empty() || name_lower.find(filter_str) != std::string::npos) {
                    ImGui::PushID(static_cast<int>(i));
                    if (ImGui::Button("Adicionar")) {
                        if (_stanceToAddTo) {
                            CreatorSubAnimationInstance newInstance;
                            // O ponteiro agora aponta para um elemento no nosso vetor _darSubMovesets
                            newInstance.sourceDef = &darSubDef;
                            strcpy_s(newInstance.editedName.data(), newInstance.editedName.size(),
                                     darSubDef.name.c_str());
                            _stanceToAddTo->subMovesets.push_back(newInstance);
                            SKSE::log::info("Adicionando animação DAR '{}' à stance.", darSubDef.name);
                        }
                    }
                    ImGui::SameLine();
                    ImGui::Text("%s", darSubDef.name.c_str());
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        ImGui::Separator();
        if (ImGui::Button("Fechar", ImVec2(120, 0))) {
            strcpy_s(darFilter, "");
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}