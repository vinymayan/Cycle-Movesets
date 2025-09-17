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
#include "ClibUtil/editorID.hpp"

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

        int filesCopied = 0;
        std::filesystem::path destinationPath = cycleDarJsonPath.parent_path();

        // NOVA LÓGICA DE PROCESSAMENTO DE FONTES MÚLTIPLAS
        auto processSource = [&](const std::string& relativePath, const rapidjson::Value* filesToCopyArray) {
            std::filesystem::path sourcePath = "Data" / std::filesystem::path(relativePath);
            if (!std::filesystem::exists(sourcePath) || !std::filesystem::is_directory(sourcePath)) {
                SKSE::log::warn("Pasta de origem não existe ou não é um diretório: {}", sourcePath.string());
                return;
            }

            SKSE::log::info("Copiando arquivos de '{}' para '{}'", sourcePath.string(), destinationPath.string());

            if (filesToCopyArray && filesToCopyArray->IsArray() && !filesToCopyArray->Empty()) {
                // Modo: Copia arquivos especificados na lista
                SKSE::log::info("Modo: Copiando arquivos especificados na lista 'filesToCopy'.");
                for (const auto& fileValue : filesToCopyArray->GetArray()) {
                    if (fileValue.IsString()) {
                        std::filesystem::path sourceFile = sourcePath / fileValue.GetString();
                        if (std::filesystem::exists(sourceFile)) {
                            CopySingleFile(sourceFile, destinationPath, filesCopied);
                        } else {
                            SKSE::log::warn("Arquivo especificado não encontrado na origem: {}", sourceFile.string());
                        }
                    }
                }
            } else {
                // Modo: Copia todos os .hkx (comportamento padrão)
                SKSE::log::info("Modo: Copiando todos os arquivos .hkx da pasta.");
                for (const auto& fileEntry : std::filesystem::directory_iterator(sourcePath)) {
                    if (fileEntry.is_regular_file() && fileEntry.path().extension() == ".hkx") {
                        CopySingleFile(fileEntry.path(), destinationPath, filesCopied);
                    }
                }
            }
        };

        if (doc.HasMember("sources") && doc["sources"].IsArray()) {
            // NOVO FORMATO: Processa o array "sources"
            for (const auto& sourceObj : doc["sources"].GetArray()) {
                if (sourceObj.IsObject() && sourceObj.HasMember("path") && sourceObj["path"].IsString()) {
                    const rapidjson::Value* filesArray =
                        sourceObj.HasMember("filesToCopy") ? &sourceObj["filesToCopy"] : nullptr;
                    processSource(sourceObj["path"].GetString(), filesArray);
                }
            }
        } else if (doc.HasMember("pathDar") && doc["pathDar"].IsString()) {
            // FORMATO ANTIGO (LEGADO): Para manter compatibilidade
            const rapidjson::Value* filesArray = doc.HasMember("filesToCopy") ? &doc["filesToCopy"] : nullptr;
            processSource(doc["pathDar"].GetString(), filesArray);
        } else {
            SKSE::log::error("Formato de CycleDar.json inválido ou não reconhecido em {}", cycleDarJsonPath.string());
            return;
        }

        SKSE::log::info("Cópia concluída. {} arquivos movidos.", filesCopied);

        // Lógica de conversão para BFCO (inalterada)
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
                    if (filename.rfind("mco_", 0) == 0) {
                        std::string newFilename = filename;
                        newFilename.replace(0, 4, "BFCO_");
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
            SKSE::log::info("Conversão BFCO concluída. {} arquivos renomeados.", filesRenamed);
        }

        // Lógica de atualização do JSON (inalterada)
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
            bool isShield;
            std::vector<std::string> keywords;
            std::vector<std::string> leftHandKeywords;
        };

        std::vector<CategoryDefinition> categoryDefinitions = {
            // Single-Wield
            {"Sword", 1.0, 0.0, false, {}, {}},
            {"Dagger", 2.0, 0.0, false, false, {}, {}},
            {"War Axe", 3.0, 0.0, false, false, {}, {}},
            {"Mace", 4.0, 0.0, false, false, {}, {}},
            {"Greatsword", 5.0, -1.0, false, false, {}, {}},
            {"Battleaxe", 6.0, -1.0, false, false, {}, {}},
            {"Warhammer", 10.0, -1.0, false, false, {}, {}},
            // Shield
            //{"Shield", -1.0, 11.0, false, true, {}, {}},
            {"Sword & Shield", 1.0, 11.0, false, true, {}, {}},
            {"Dagger & Shield", 2.0, 11.0, false, true, {}, {}},
            {"War Axe & Shield", 3.0, 11.0, false, true, {}, {}},
            {"Mace & Shield", 4.0, 11.0, false, true, {}, {}},
            {"Greatsword & Shield", 5.0, 11.0, false, true, {}, {}},
            {"Battleaxe & Shield", 6.0, 11.0, false, true, {}, {}},
            {"Warhammer & Shield", 10.0, 11.0, false, true, {}, {}},
            // Dual-Wield
            {"Dual Sword", 1.0, 1.0, true, {}, {}},
            {"Dual Dagger", 2.0, 2.0, true, {}, {}},
            {"Dual War Axe", 3.0, 3.0, true, {}, {}},
            {"Dual Mace", 4.0, 4.0, true, {}, {}},
            {"Unarmed", 0.0, 0.0, true, {}, {}}
        };

        for (const auto& def : categoryDefinitions) {
            _categories[def.name].name = def.name;
            _categories[def.name].equippedTypeValue = def.typeValue;
            _categories[def.name].leftHandEquippedTypeValue = def.leftHandTypeValue;
            _categories[def.name].isDualWield = def.isDual;
            _categories[def.name].isShieldCategory = def.isShield;
            _categories[def.name].keywords = def.keywords;
            _categories[def.name].leftHandKeywords = def.leftHandKeywords;
            _categories[def.name].isCustom = false;
            _categories[def.name].baseCategoryName = "Base";

            // --- NOVO: Inicializa os nomes e buffers das stances ---
            for (int i = 0; i < 4; ++i) {
                std::string defaultName = std::format("Stance {}", i + 1);
                _categories[def.name].stanceNames[i] = defaultName;
                strcpy_s(_categories[def.name].stanceNameBuffers[i].data(),
                         _categories[def.name].stanceNameBuffers[i].size(), defaultName.c_str());
            }
        }
        LoadCustomCategories();
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
        //LoadUserMovesets();

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
        PopulateNpcList();
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
            ImGui::InputText(LOC("filter"), _movesetFilter, 128);
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
            ImGui::InputText(LOC("filter"), _subMovesetFilter, 128);

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
                                    float button_width = 200.0f;

                                    ImVec2 content_avail;
                                    ImGui::GetContentRegionAvail(&content_avail);  // Pega a região disponível

                                    if (ImGui::Button(LOC("add"))) {
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
                                            PopulateHkxFiles(newInstance);
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
                                    ImGui::PopID();  // <-- CORREÇÃO 2: PopID movido para DENTRO do loop
                                                     
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
            if (ImGui::BeginTabItem(LOC("category_manager"))) {
                DrawCategoryManager();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        DrawAddModModal();
        DrawAddDarModal();
        DrawStanceEditorPopup();
        DrawRestartPopup();
        DrawCreateCategoryModal();
        
    }

    // Nova função para desenhar a interface de criação de movesets
    void AnimationManager::DrawUserMovesetCreator() {
        ImGui::Text("Moveset Creator");
        ImGui::Separator();

        // Seção de botões principais
        if (ImGui::Button(LOC("save"))) {
            SaveUserMoveset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Read DAR animations")) {
            ScanDarAnimations();
        }
        ImGui::Separator();

        // Campos de Informação do Moveset
        ImGui::InputText("Moveset Name", _newMovesetName, sizeof(_newMovesetName));
        ImGui::InputText("Author", _newMovesetAuthor, sizeof(_newMovesetAuthor));
        ImGui::InputText("Descripton", _newMovesetDesc, sizeof(_newMovesetDesc));

        ImGui::Separator();

        ImGui::Text("Select categories");
        ImGui::InputText(LOC("filter"), _categoryFilterBuffer, sizeof(_categoryFilterBuffer));
        if (ImGui::BeginChild("CategorySelector", ImVec2(0, 150), true)) {
            std::string filter_str = _categoryFilterBuffer;
            std::transform(filter_str.begin(), filter_str.end(), filter_str.begin(), ::tolower);

            for (const auto& pair : _categories) {
                std::string category_name_lower = pair.first;
                std::transform(category_name_lower.begin(), category_name_lower.end(), category_name_lower.begin(),
                               ::tolower);

                if (filter_str.empty() || category_name_lower.find(filter_str) != std::string::npos) {
                    if (_newMovesetCategorySelection.find(pair.first) == _newMovesetCategorySelection.end()) {
                        _newMovesetCategorySelection[pair.first] = false;
                    }
                    ImGui::Checkbox(pair.first.c_str(), &_newMovesetCategorySelection[pair.first]);
                }
            }
        }
        ImGui::EndChild();
        ImGui::Separator();

        ImGui::Text("Add animations");

        for (auto const& [categoryName, isSelected] : _newMovesetCategorySelection) {
            if (isSelected) {
                ImGui::PushID(categoryName.c_str());

                if (ImGui::CollapsingHeader(categoryName.c_str())) {
                    if (_movesetCreatorStances.find(categoryName) == _movesetCreatorStances.end()) {
                        _movesetCreatorStances[categoryName] = std::array<CreatorStance, 4>();
                    }
                    auto& stances = _movesetCreatorStances.at(categoryName);

                    if (ImGui::BeginTabBar(std::string("StanceTabs_" + categoryName).c_str())) {
                        for (int i = 0; i < 4; ++i) {
                            std::string stanceTabName = std::format("Stance {}", i + 1);
                            if (ImGui::BeginTabItem(stanceTabName.c_str())) {
                                if (ImGui::Button(std::format("Add animation to {}", i + 1).c_str())) {
                                    _isAddModModalOpen = true;
                                    _stanceToAddTo = &stances[i];
                                    _instanceToAddTo = nullptr;
                                    _modInstanceToAddTo = nullptr;
                                    _userMovesetToAddTo = nullptr;
                                }
                                ImGui::SameLine();
                                if (ImGui::Button(std::format("Add DAR animation to {}", i + 1).c_str())) {
                                    _isAddDarModalOpen = true;
                                    _stanceToAddTo = &stances[i];
                                    _instanceToAddTo = nullptr;
                                    _modInstanceToAddTo = nullptr;
                                    _userMovesetToAddTo = nullptr;
                                }
                                ImGui::Separator();

                                int subToRemove = -1;
                                int moveUpIndex = -1;
                                int moveDownIndex = -1;

                                for (size_t j = 0; j < stances[i].subMovesets.size(); ++j) {
                                    auto& subInst = stances[i].subMovesets[j];
                                    ImGui::PushID(static_cast<int>(j));

                                    // --- INÍCIO DA ADIÇÃO #2: BOTÕES DE REORDENAMENTO ---
                                    if (j > 0) {
                                        if (ImGui::Button("Up")) {
                                            moveUpIndex = j;
                                        }
                                        ImGui::SameLine();
                                    }
                                    if (j < stances[i].subMovesets.size() - 1) {
                                        if (ImGui::Button("Down")) {
                                            moveDownIndex = j;
                                        }
                                        ImGui::SameLine();
                                    }
                                    // --- FIM DA ADIÇÃO #2 ---

                                    if (ImGui::Button("X")) {
                                        subToRemove = j;
                                    }
                                    ImGui::SameLine();
                                    ImGui::InputText("##SubName", subInst.editedName.data(), subInst.editedName.size());
                                    ImGui::SameLine();
                                    ImGui::Text("<- %s", subInst.sourceDef->name.c_str());
                                    ImGui::SameLine();
                                    ImGui::Checkbox("ToBFCO", &subInst.isBFCO);

                                    ImGui::Indent();
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

                                    // Seção para gerenciar arquivos .hkx individuais
                                    if (!subInst.hkxFileSelection.empty()) {
                                        int selectedCount = 0;
                                        for (const auto& pair : subInst.hkxFileSelection) {
                                            if (pair.second) selectedCount++;
                                        }
                                        
                                        if (ImGui::CollapsingHeader("Manage Animation Files")) {
                                            ImGui::Indent();
                                            ImGui::TextDisabled("Deselect files you do not want to include:");
                                            if (ImGui::BeginChild("HkxFilesChild", ImVec2(0, 300), true)) {
                                                for (auto& [filename, isFileSelected] : subInst.hkxFileSelection) {
                                                    ImGui::Checkbox(filename.c_str(), &isFileSelected);
                                                }
                                            }
                                            ImGui::EndChild();
                                            ImGui::Unindent();
                                        }
                                    }

                                    ImGui::PopID();
                                    ImGui::Separator();
                                }

                                if (subToRemove != -1) {
                                    stances[i].subMovesets.erase(stances[i].subMovesets.begin() + subToRemove);
                                }
                                // --- INÍCIO DA ADIÇÃO #3: APLICA A MUDANÇA DE ORDEM ---
                                if (moveUpIndex != -1) {
                                    std::swap(stances[i].subMovesets[moveUpIndex],
                                              stances[i].subMovesets[moveUpIndex - 1]);
                                }
                                if (moveDownIndex != -1) {
                                    std::swap(stances[i].subMovesets[moveDownIndex],
                                              stances[i].subMovesets[moveDownIndex + 1]);
                                }

                                ImGui::EndTabItem();
                            }
                        }
                        ImGui::EndTabBar();
                    }
                }
                ImGui::PopID();
            }
        }
    }

    void AnimationManager::DrawNPCMenu() { 
        DrawNPCManager();
        DrawAddModModal();
        DrawRestartPopup();
        DrawNpcSelectionModal();
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

    int AnimationManager::GetMaxMovesetsForNPC(RE::FormID npcFormID, const std::string& category, int stanceIndex) {
        if (stanceIndex < 0 || stanceIndex >= 4) {
            return 0;
        }
        // 1. Tenta encontrar a configuração para o NPC ESPECÍFICO
        auto specificNpcIt = _maxMovesetsPerCategory_NPC.find(npcFormID);
        if (specificNpcIt != _maxMovesetsPerCategory_NPC.end()) {
            // Encontrou o FormID do NPC no cache
            const auto& categoriesForNpc = specificNpcIt->second;
            auto categoryIt = categoriesForNpc.find(category);
            if (categoryIt != categoriesForNpc.end()) {
                // Encontrou a categoria para este NPC específico, retorna a contagem
                return categoryIt->second[stanceIndex];
            }
        }

        // 2. Se não encontrou para o NPC específico, usa o FALLBACK para NPCs GERAIS (FormID 0)
        auto generalNpcIt = _maxMovesetsPerCategory_NPC.find(0);
        if (generalNpcIt != _maxMovesetsPerCategory_NPC.end()) {
            const auto& generalCategories = generalNpcIt->second;
            auto categoryIt = generalCategories.find(category);
            if (categoryIt != generalCategories.end()) {
                // Encontrou a categoria nas configurações gerais, retorna a contagem
                return categoryIt->second[stanceIndex];
            }
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
                                ImGui::Text("Move moveset %s", sourceMod.name.c_str());
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
                                        ImGui::Text("Move %s", originSubAnim.name.c_str());
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
            if (ImGui::BeginTabItem(LOC("tab_single_wield"))) {
                for (auto& pair : _categories) {
                    WeaponCategory& category = pair.second;
                    if (!category.isDualWield && !category.isShieldCategory) {  // Filtro
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
            if (ImGui::BeginTabItem(LOC("tab_shield"))) {  
                for (auto& pair : _categories) {
                    WeaponCategory& category = pair.second;
                    if (category.isShieldCategory) {
                        DrawCategoryUI(pair.second);

                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

    }


// INÍCIO DA SUBSTITUIÇÃO (Função DrawNPCManager)
    void AnimationManager::DrawNPCManager() {
        if (ImGui::Button(LOC("save"))) {
            SaveAllSettings();
        }
        ImGui::SameLine();
        if (ImGui::Button("Add/Manage NPCs")) {
            _isNpcSelectionModalOpen = true;
        }
        ImGui::Separator();

        _npcSelectorList.clear();
        _npcSelectorList.push_back("NPCs (General)");
        static std::vector<std::string> npcSelectorLabels;
        npcSelectorLabels.clear();

        // Constrói as labels diretamente da nossa estrutura de dados carregada
        for (const auto& pair : _specificNpcConfigs) {
            RE::FormID formID = pair.first;
            const SpecificNpcConfig& config = pair.second;
            npcSelectorLabels.push_back(std::format("{} ({:08X})", config.name, formID));
        }
        for (const auto& label : npcSelectorLabels) {
            _npcSelectorList.push_back(label.c_str());
        }

        int currentSelection = 0;
        if (_currentlySelectedNpcFormID == 0) {
            currentSelection = 0;
        } else {
            int i = 1;
            for (const auto& pair : _specificNpcConfigs) {
                if (pair.first == _currentlySelectedNpcFormID) {
                    currentSelection = i;
                    break;
                }
                i++;
            }
        }

        if (ImGui::Combo(LOC("menu_npc"), &currentSelection, _npcSelectorList.data(), _npcSelectorList.size())) {
            if (currentSelection == 0) {
                _currentlySelectedNpcFormID = 0;
            } else {
                int i = 1;
                for (const auto& pair : _specificNpcConfigs) {
                    if (i == currentSelection) {
                        _currentlySelectedNpcFormID = pair.first;
                        break;
                    }
                    i++;
                }
            }
        }
        ImGui::Separator();

        std::map<std::string, WeaponCategory>* categoriesToDraw =
            (_currentlySelectedNpcFormID == 0) ? &_npcCategories
                                               : &_specificNpcConfigs.at(_currentlySelectedNpcFormID).categories;

        if (categoriesToDraw->empty()) {
            ImGui::Text("Nenhuma categoria de animação foi carregada.");
            return;
        }

        if (ImGui::BeginTabBar("WeaponTypeTabs_NPC")) {
            if (ImGui::BeginTabItem(LOC("tab_single_wield"))) {
                for (auto& pair : *categoriesToDraw) {
                    if (!pair.second.isDualWield && !pair.second.isShieldCategory) {
                        DrawNPCCategoryUI(pair.second);
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(LOC("tab_dual_wield"))) {
                for (auto& pair : *categoriesToDraw) {
                    if (pair.second.isDualWield) {
                        DrawNPCCategoryUI(pair.second);
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(LOC("tab_shield"))) {
                for (auto& pair : *categoriesToDraw) {
                    if (pair.second.isShieldCategory) {
                        DrawNPCCategoryUI(pair.second);
                    }
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    // FIM DA SUBSTITUIÇÃO

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
                    ImGui::Text("Move moveset %s", sourceMod.name.c_str());
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
                            label += std::format(" (from: {})", originMod.name);
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
        SaveCustomCategories();
        SaveStanceNames();
        SaveCycleMovesets();  // Esta já foi corrigida e está funcionando.
        SKSE::log::info("Gerando arquivos de condição para OAR...");
        std::map<std::filesystem::path, std::vector<FileSaveConfig>> fileUpdates;

        // 1. Processa as categorias do JOGADOR
        for (auto& pair : _categories) {
            WeaponCategory& category = pair.second;
            for (int i = 0; i < 4; ++i) {  // Stances
                CategoryInstance& instance = category.instances[i];
                int playlistParentCounter = 1;
                int lastParentOrder = 0;
                for (auto& modInstance : instance.modInstances) {
                    if (!modInstance.isSelected) continue;
                    for (auto& subInstance : modInstance.subAnimationInstances) {
                        if (!subInstance.isSelected) continue;

                        const auto& sourceSubAnim =
                            _allMods[subInstance.sourceModIndex].subAnimations[subInstance.sourceSubAnimIndex];
                        FileSaveConfig config;
                        config.isNPC = false;
                        config.instance_index = i + 1;
                        config.category = &category;
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

                        bool isParent = !(config.pFront || config.pBack || config.pLeft || config.pRight ||
                                          config.pFrontRight || config.pFrontLeft || config.pBackRight ||
                                          config.pBackLeft || config.pRandom || config.pDodge);
                        config.isParent = isParent;
                        if (isParent) {
                            lastParentOrder = playlistParentCounter;
                            config.order_in_playlist = playlistParentCounter++;
                        } else {
                            config.order_in_playlist = lastParentOrder;
                        }
                        fileUpdates[sourceSubAnim.path].push_back(config);
                    }
                }
            }
        }

        // 2. Processa as categorias dos NPCS GERAIS
        for (auto& pair : _npcCategories) {
            WeaponCategory& category = pair.second;
            CategoryInstance& instance = category.instances[0];  // NPCs usam stance 0
            int playlistCounter = 1;
            for (auto& modInstance : instance.modInstances) {
                if (!modInstance.isSelected) continue;
                for (auto& subInstance : modInstance.subAnimationInstances) {
                    if (!subInstance.isSelected) continue;

                    const auto& sourceSubAnim =
                        _allMods[subInstance.sourceModIndex].subAnimations[subInstance.sourceSubAnimIndex];
                    FileSaveConfig config;
                    config.isNPC = true;
                    config.npcFormID = 0;  // 0 indica NPC Geral
                    config.instance_index = 0;
                    config.category = &category;
                    config.isParent = true;
                    config.order_in_playlist = playlistCounter++;
                    config.pRandom = subInstance.pRandom;
                    config.pDodge = subInstance.pDodge;
                    fileUpdates[sourceSubAnim.path].push_back(config);
                }
            }
        }

        // 3. NOVO: Processa as categorias dos NPCS ESPECÍFICOS
        for (const auto& npcConfigPair : _specificNpcConfigs) {
            RE::FormID specificNpcId = npcConfigPair.first;
            const SpecificNpcConfig& npcConfig = npcConfigPair.second;  // Acessa a struct completa

            // A busca pelo pluginName não é mais necessária, já o temos!
            const std::string& npcPluginName = npcConfig.pluginName;

            // Itera sobre o mapa 'categories' dentro da struct
            for (const auto& pair : npcConfig.categories) {
                const WeaponCategory& category = pair.second;
                const CategoryInstance& instance = category.instances[0];
                int playlistCounter = 1;
                for (const auto& modInstance : instance.modInstances) {
                    if (!modInstance.isSelected) continue;
                    for (const auto& subInstance : modInstance.subAnimationInstances) {
                        if (!subInstance.isSelected) continue;

                        const auto& sourceSubAnim =
                            _allMods[subInstance.sourceModIndex].subAnimations[subInstance.sourceSubAnimIndex];
                        FileSaveConfig config;
                        config.isNPC = true;
                        config.npcFormID = specificNpcId;   // ID específico do NPC
                        config.pluginName = npcPluginName;  // Nome do plugin (direto da struct)
                        config.instance_index = 0;
                        config.category = &category;
                        config.isParent = true;
                        config.order_in_playlist = playlistCounter++;
                        config.pRandom = subInstance.pRandom;
                        config.pDodge = subInstance.pDodge;
                        fileUpdates[sourceSubAnim.path].push_back(config);
                    }
                }
            }
        }

        // Lógica para desativar arquivos que não são mais usados (sem alteração)
        for (const auto& managedPath : _managedFiles) {
            if (fileUpdates.find(managedPath) == fileUpdates.end()) {
                fileUpdates[managedPath] = {};
            }
        }
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

        std::string movesetName = jsonPath.parent_path().filename().string();
        if (doc.HasMember("name")) {
            doc["name"].SetString(movesetName.c_str(), allocator);
        } else {
            doc.AddMember("name", rapidjson::Value(movesetName.c_str(), allocator), allocator);
        }

        int basePriority = 2000000000;
        bool isUsedAsParent = false;
        for (const auto& config : configs) {
            if (config.isParent) {
                isUsedAsParent = true;
                break;
            }
        }
        int finalPriority = isUsedAsParent ? basePriority : basePriority + 1;

        if (doc.HasMember("priority")) {
            doc["priority"].SetInt(finalPriority);
        } else {
            doc.AddMember("priority", finalPriority, allocator);
        }

        rapidjson::Value oldConditions(rapidjson::kArrayType);
        if (_preserveConditions && doc.HasMember("conditions") && doc["conditions"].IsArray()) {
            for (auto& cond : doc["conditions"].GetArray()) {
                if (cond.IsObject() && cond.HasMember("comment") && cond["comment"] == "OAR_CYCLE_MANAGER_CONDITIONS") {
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

        std::map<int, std::set<int>> childDirectionsByPlaylist;
        for (const auto& config : configs) {
            if (!config.isParent) {
                int playlistId = config.order_in_playlist;
                if (playlistId > 0) {
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

                // ActorBase condition
                {
                    rapidjson::Value actorBase(rapidjson::kObjectType);
                    actorBase.AddMember("condition", "IsActorBase", allocator);
                    rapidjson::Value actorBaseParams(rapidjson::kObjectType);

                    if (config.npcFormID != 0) {  // CASO 1: NPC ESPECÍFICO
                        actorBase.AddMember("negated", false, allocator);
                        actorBaseParams.AddMember("pluginName", rapidjson::Value(config.pluginName.c_str(), allocator),
                                                  allocator);

                        // Formata o FormID para os últimos 5 dígitos
                        std::string fullFormIDStr = std::format("{:08X}", config.npcFormID);
                        std::string shortFormIDStr = fullFormIDStr.substr(3);  // Pega de "000A2C8E" -> "A2C8E"
                        actorBaseParams.AddMember("formID", rapidjson::Value(shortFormIDStr.c_str(), allocator),
                                                  allocator);

                    } else if (config.isNPC) {  // CASO 2: NPC GERAL
                        actorBase.AddMember("negated", true, allocator);
                        actorBaseParams.AddMember("pluginName", "Skyrim.esm", allocator);
                        actorBaseParams.AddMember("formID", "7", allocator);  // Nega o Player

                    } else {  // CASO 3: JOGADOR
                        actorBase.AddMember("negated", false, allocator);
                        actorBaseParams.AddMember("pluginName", "Skyrim.esm", allocator);
                        actorBaseParams.AddMember("formID", "7", allocator);
                    }
                    actorBase.AddMember("Actor base", actorBaseParams, allocator);
                    andConditions.PushBack(actorBase, allocator);

                    // NOVO: Se for NPC Geral, adiciona a exclusão de NPCs específicos
                    if (config.isNPC && config.npcFormID == 0 && !_specificNpcConfigs.empty()) {
                        rapidjson::Value exclusionAndBlock(rapidjson::kObjectType);
                        exclusionAndBlock.AddMember("condition", "AND", allocator);
                        exclusionAndBlock.AddMember("comment", "Exclude specific NPCs", allocator);
                        rapidjson::Value exclusionConditions(rapidjson::kArrayType);

                        for (const auto& npcPair : _specificNpcConfigs) {
                            RE::FormID idToExclude = npcPair.first;
                            std::string pluginToExclude;
                            for (const auto& npcInfo : _fullNpcList) {
                                if (npcInfo.formID == idToExclude) {
                                    pluginToExclude = npcInfo.pluginName;
                                    break;
                                }
                            }
                            if (!pluginToExclude.empty()) {
                                rapidjson::Value negatedActor(rapidjson::kObjectType);
                                negatedActor.AddMember("condition", "IsActorBase", allocator);
                                negatedActor.AddMember("negated", true, allocator);
                                rapidjson::Value negatedParams(rapidjson::kObjectType);
                                negatedParams.AddMember(
                                    "pluginName", rapidjson::Value(pluginToExclude.c_str(), allocator), allocator);
                                std::string fullIdStr = std::format("{:08X}", idToExclude);
                                std::string shortIdStr = fullIdStr.substr(3);
                                negatedParams.AddMember("formID", rapidjson::Value(shortIdStr.c_str(), allocator),
                                                        allocator);
                                negatedActor.AddMember("Actor base", negatedParams, allocator);
                                exclusionConditions.PushBack(negatedActor, allocator);
                            }
                        }
                        if (!exclusionConditions.Empty()) {
                            exclusionAndBlock.AddMember("Conditions", exclusionConditions, allocator);
                            andConditions.PushBack(exclusionAndBlock, allocator);
                        }
                    }
                }

                // Right-Hand Equipped Type condition
                {
                    if (config.category->equippedTypeValue < 0.0) {  // Categoria genérica como "Shield"
                        rapidjson::Value rightHandAndBlock(rapidjson::kObjectType);
                        rightHandAndBlock.AddMember("condition", "AND", allocator);
                        rapidjson::Value conditionsForRightHand(rapidjson::kArrayType);

                        rapidjson::Value orBlock(rapidjson::kObjectType);
                        orBlock.AddMember("condition", "OR", allocator);
                        rapidjson::Value orConditions(rapidjson::kArrayType);
                        AddCompareEquippedTypeCondition(orConditions, 1.0, false, allocator);
                        AddCompareEquippedTypeCondition(orConditions, 2.0, false, allocator);
                        AddCompareEquippedTypeCondition(orConditions, 3.0, false, allocator);
                        AddCompareEquippedTypeCondition(orConditions, 4.0, false, allocator);
                        orBlock.AddMember("Conditions", orConditions, allocator);
                        conditionsForRightHand.PushBack(orBlock, allocator);

                        // CORREÇÃO #2: A lógica de exclusão para a categoria Shield base agora é chamada corretamente.
                        AddShieldCategoryExclusions(conditionsForRightHand, allocator);

                        rightHandAndBlock.AddMember("Conditions", conditionsForRightHand, allocator);
                        andConditions.PushBack(rightHandAndBlock, allocator);
                    } else {  // Caso padrão
                        AddCompareEquippedTypeCondition(andConditions, config.category->equippedTypeValue, false,
                                                        allocator);
                    }
                }

                // Right-Hand Keyword conditions
                AddKeywordOrConditions(andConditions, config.category->keywords, false, allocator);
                AddCompetingKeywordExclusions(andConditions, config.category, false, allocator);

                // Left-Hand Keyword conditions
                if (!config.category->leftHandKeywords.empty()) {
                    AddKeywordOrConditions(andConditions, config.category->leftHandKeywords, true, allocator);
                    AddCompetingKeywordExclusions(andConditions, config.category, true, allocator);
                }

                // Left-Hand Equipped Type condition
                if (config.category->leftHandEquippedTypeValue >= 0.0) {
                    AddCompareEquippedTypeCondition(andConditions, config.category->leftHandEquippedTypeValue, true,
                                                    allocator);

                    // CORREÇÃO #2: Bloco que causava duplicação de keywords foi removido daqui.
                    // A verificação de keywords da mão esquerda já é feita acima.
                }

                // ADIÇÃO: Correção de segurança para a instância do jogador
                int final_instance_index = config.instance_index;
                // Se a configuração é para o jogador (!isNPC) e o índice for inválido (< 1), corrige para 1.
                if (!config.isNPC && final_instance_index < 1) {
                    SKSE::log::warn(
                        "Índice de instância inválido (0) encontrado para o Jogador em {}. Corrigindo para 1.",
                        jsonPath.string());
                    final_instance_index = 1;  // Garante que o valor mínimo para o jogador seja 1
                }
                // Stance and Playlist order
                AddCompareValuesCondition(andConditions, "cycle_instance", final_instance_index, allocator);
                if (config.order_in_playlist > 0) {
                    AddCompareValuesCondition(andConditions, "testarone", config.order_in_playlist, allocator);
                    if (config.isParent) {
                        const auto& childDirs = childDirectionsByPlaylist[config.order_in_playlist];
                        if (!childDirs.empty()) {
                            for (int dirValue : childDirs) {
                                AddNegatedCompareValuesCondition(andConditions, "DirecionalCycleMoveset", dirValue,
                                                                 allocator);
                            }
                        }
                    } else {
                        if (config.pRandom) {
                            AddRandomCondition(andConditions, config.order_in_playlist, allocator);
                        }
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
                        if (!directionalOrConditions.Empty()) {
                            rapidjson::Value orBlock(rapidjson::kObjectType);
                            orBlock.AddMember("condition", "OR", allocator);
                            orBlock.AddMember("Conditions", directionalOrConditions, allocator);
                            andConditions.PushBack(orBlock, allocator);
                        }
                    }
                }

                categoryAndBlock.AddMember("Conditions", andConditions, allocator);
                innerConditions.PushBack(categoryAndBlock, allocator);
            }
            if (!innerConditions.Empty()) {
                masterOrBlock.AddMember("Conditions", innerConditions, allocator);
                conditions.PushBack(masterOrBlock, allocator);
            }
        } else {  // "Kill switch" condition
            rapidjson::Value masterOrBlock(rapidjson::kObjectType);
            masterOrBlock.AddMember("condition", "OR", allocator);
            masterOrBlock.AddMember("comment", "OAR_CYCLE_MANAGER_CONDITIONS", allocator);
            rapidjson::Value innerConditions(rapidjson::kArrayType);
            rapidjson::Value andBlock(rapidjson::kObjectType);
            andBlock.AddMember("condition", "AND", allocator);
            rapidjson::Value andConditions(rapidjson::kArrayType);
            AddCompareValuesCondition(andConditions, "CycleMovesetDisable", 1, allocator);
            andBlock.AddMember("Conditions", andConditions, allocator);
            innerConditions.PushBack(andBlock, allocator);
            masterOrBlock.AddMember("Conditions", innerConditions, allocator);
            conditions.PushBack(masterOrBlock, allocator);
        }

        // Save the document
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
        _maxMovesetsPerCategory_NPC.clear();

        // 1. Cache do JOGADOR (lógica inalterada)
        for (auto& pair : _categories) {
            WeaponCategory& category = pair.second;
            std::array<int, 4> counts = {0, 0, 0, 0};
            for (int i = 0; i < 4; ++i) {
                CategoryInstance& instance = category.instances[i];
                int parentMovesetCount = 0;
                for (auto& modInst : instance.modInstances) {
                    if (!modInst.isSelected) continue;
                    for (auto& subInst : modInst.subAnimationInstances) {
                        if (!subInst.isSelected) continue;
                        const auto& sourceSubAnim =
                            _allMods[subInst.sourceModIndex].subAnimations[subInst.sourceSubAnimIndex];
                        if (!sourceSubAnim.hasAnimations) {
                            continue;
                        }
                        bool isParent = !(subInst.pFront || subInst.pBack || subInst.pLeft || subInst.pRight ||
                                          subInst.pFrontRight || subInst.pFrontLeft || subInst.pBackRight ||
                                          subInst.pBackLeft || subInst.pRandom || subInst.pDodge);
                        if (isParent) {
                            parentMovesetCount++;
                        }
                    }
                }
                counts[i] = parentMovesetCount;
            }
            _maxMovesetsPerCategory[category.name] = counts;
        }
        SKSE::log::info("Cache do Jogador atualizado.");

        // 2. Cache dos NPCS GERAIS (usando FormID 0 como chave)
        for (auto& pair : _npcCategories) {
            WeaponCategory& category = pair.second;
            CategoryInstance& instance = category.instances[0];  // Stance 0 para NPCs
            int npcMovesetCount = 0;
            for (auto& modInst : instance.modInstances) {
                if (modInst.isSelected) {
                    for (auto& subInst : modInst.subAnimationInstances) {
                        if (subInst.isSelected) {
                            npcMovesetCount++;
                        }
                    }
                }
            }
            std::array<int, 4> npc_counts = {npcMovesetCount, 0, 0, 0};
            _maxMovesetsPerCategory_NPC[0][category.name] = npc_counts;
        }
        SKSE::log::info("Cache de NPCs Gerais (ID 0) atualizado.");

        // 3. Cache dos NPCS ESPECÍFICOS (usando seu FormID real como chave)
        for (const auto& npcConfigPair : _specificNpcConfigs) {
            RE::FormID npcFormID = npcConfigPair.first;
            const SpecificNpcConfig& npcConfig = npcConfigPair.second;

            for (const auto& catPair : npcConfig.categories) {
                const WeaponCategory& category = catPair.second;
                const CategoryInstance& instance = category.instances[0];
                int npcMovesetCount = 0;
                for (const auto& modInst : instance.modInstances) {
                    if (modInst.isSelected) {
                        for (const auto& subInst : modInst.subAnimationInstances) {
                            if (subInst.isSelected) {
                                npcMovesetCount++;
                            }
                        }
                    }
                }
                std::array<int, 4> npc_counts = {npcMovesetCount, 0, 0, 0};
                _maxMovesetsPerCategory_NPC[npcFormID][category.name] = npc_counts;
            }
            SKSE::log::info("Cache para NPC Específico {:08X} atualizado.", npcFormID);
        }

        SKSE::log::info("Cache de contagem máxima de movesets (Player & Todos NPCs) foi atualizado.");
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
    //void AnimationManager::AddOcfWeaponExclusionConditions(rapidjson::Value& parentArray,
    //                                                       rapidjson::Document::AllocatorType& allocator) {
    //    rapidjson::Value mainAndBlock(rapidjson::kObjectType);
    //    mainAndBlock.AddMember("condition", "AND", allocator);
    //    mainAndBlock.AddMember("requiredVersion", "1.0.0.0", allocator);

    //    rapidjson::Value innerConditions(rapidjson::kArrayType);

    //    // Lista dos editorIDs das armas a serem excluídas
    //    const std::vector<const char*> keywords = {
    //        "OCF_WeapTypeRapier1H", "OCF_WeapTypeRapier2H",    "OCF_WeapTypeKatana1H",   "OCF_WeapTypeKatana2H",
    //        "OCF_WeapTypePike1H",   "OCF_WeapTypePike2H",      "OCF_WeapTypeHalberd2H",  "OCF_WeapTypeHalberd1H",
    //        "OCF_WeapTypeClaw1H",   "OCF_WeapTypeTwinblade1H", "OCF_WeapTypeTwinblade2H"};

    //    // Função lambda para criar uma condição "IsEquippedHasKeyword" negada
    //    auto createNegatedKeywordCondition = [&](const char* editorID, bool isLeftHand) {
    //        rapidjson::Value condition(rapidjson::kObjectType);
    //        condition.AddMember("condition", "IsEquippedHasKeyword", allocator);
    //        condition.AddMember("requiredVersion", "1.0.0.0", allocator);
    //        condition.AddMember("negated", true, allocator);

    //        rapidjson::Value keywordObj(rapidjson::kObjectType);
    //        keywordObj.AddMember("editorID", rapidjson::StringRef(editorID), allocator);
    //        condition.AddMember("Keyword", keywordObj, allocator);

    //        condition.AddMember("Left hand", isLeftHand, allocator);
    //        return condition;
    //    };

    //    // Adiciona as condições para a maioria das armas (mão direita e esquerda)
    //    for (const auto& keyword : keywords) {
    //        innerConditions.PushBack(createNegatedKeywordCondition(keyword, false), allocator);  // Left hand: false
    //        innerConditions.PushBack(createNegatedKeywordCondition(keyword, true), allocator);   // Left hand: true
    //    }

    //    // Casos especiais do Quarterstaff que não seguem o padrão par
    //    innerConditions.PushBack(createNegatedKeywordCondition("OCF_WeapTypeQuarterstaff2H", false), allocator);
    //    innerConditions.PushBack(createNegatedKeywordCondition("OCF_WeapTypeQuarterstaff1H", true), allocator);

    //    mainAndBlock.AddMember("Conditions", innerConditions, allocator);
    //    parentArray.PushBack(mainAndBlock, allocator);
    //}

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


// INÍCIO DA SUBSTITUIÇÃO (Função SaveCycleMovesets)
    void AnimationManager::SaveCycleMovesets() {
        SKSE::log::info("Iniciando salvamento de CycleMoveset.json para Player, NPCs Gerais e Específicos...");

        std::map<std::filesystem::path, std::unique_ptr<rapidjson::Document>> documents;
        std::set<std::filesystem::path> requiredFiles;

        auto processActorCategories = [&](const std::map<std::string, WeaponCategory>& sourceCategories,
                                          const std::string& actorName, const std::string& actorFormID = "",
                                          const std::string& actorPlugin = "") {
            for (const auto& pair : sourceCategories) {
                const WeaponCategory& category = pair.second;
                for (int i = 0; i < 4; ++i) {
                    const CategoryInstance& instance = category.instances[i];
                    for (const auto& modInst : instance.modInstances) {
                        if (!modInst.isSelected) continue;
                        const auto& sourceMod = _allMods[modInst.sourceModIndex];
                        int animationIndexCounter = 1;
                        for (const auto& subInst : modInst.subAnimationInstances) {
                            if (!subInst.isSelected) continue;

                            const auto& animOriginSub =
                                _allMods[subInst.sourceModIndex].subAnimations[subInst.sourceSubAnimIndex];
                            std::filesystem::path cycleJsonPath =
                                animOriginSub.path.parent_path() / "CycleMoveset.json";
                            requiredFiles.insert(cycleJsonPath);

                            if (documents.find(cycleJsonPath) == documents.end()) {
                                documents[cycleJsonPath] = std::make_unique<rapidjson::Document>();
                                documents[cycleJsonPath]->SetArray();
                            }
                            rapidjson::Document& doc = *documents[cycleJsonPath];
                            auto& allocator = doc.GetAllocator();

                            rapidjson::Value* actorObj = nullptr;
                            for (auto& item : doc.GetArray()) {
                                if (item.IsObject() && item.HasMember("FormID") &&
                                    item["FormID"].GetString() == actorFormID) {
                                    actorObj = &item;
                                    break;
                                }
                            }

                            if (!actorObj) {
                                rapidjson::Value newActorObj(rapidjson::kObjectType);
                                newActorObj.AddMember("Name", rapidjson::Value(actorName.c_str(), allocator),
                                                      allocator);
                                // Para Player e NPCs Gerais, o FormID é o próprio nome para garantir unicidade
                                newActorObj.AddMember(
                                    "FormID",
                                    rapidjson::Value(actorFormID.empty() ? actorName.c_str() : actorFormID.c_str(),
                                                     allocator),
                                    allocator);
                                if (!actorPlugin.empty()) {
                                    newActorObj.AddMember("Plugin", rapidjson::Value(actorPlugin.c_str(), allocator),
                                                          allocator);
                                }
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
                                if (item.IsObject() && item.HasMember("index") && item["index"].GetInt() == (i + 1) &&
                                    item.HasMember("name") &&
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
                            animObj.AddMember(
                                "sourceModName",
                                rapidjson::Value(_allMods[subInst.sourceModIndex].name.c_str(), allocator), allocator);
                            animObj.AddMember("sourceSubName", rapidjson::Value(animOriginSub.name.c_str(), allocator),
                                              allocator);
                            animObj.AddMember("sourceConfigPath",
                                              rapidjson::Value(animOriginSub.path.string().c_str(), allocator),
                                              allocator);
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

        for (const auto& npcPair : _specificNpcConfigs) {
            RE::FormID npcFormID = npcPair.first;
            const SpecificNpcConfig& npcConfig = npcPair.second;  // Acessa a struct

            std::string npcFormIDStr = std::format("{:08X}", npcFormID);

            // Chama a função com os dados diretamente da struct, sem precisar de uma nova busca
            processActorCategories(npcConfig.categories, npcConfig.name, npcFormIDStr, npcConfig.pluginName);
        }

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
    // FIM DA SUBSTITUIÇÃO

// INÍCIO DA SUBSTITUIÇÃO (Função LoadCycleMovesets)
    void AnimationManager::LoadCycleMovesets() {
        SKSE::log::info("Iniciando carregamento e fusão de arquivos CycleMoveset.json...");

        for (auto& pair : _categories) {
            for (auto& instance : pair.second.instances) instance.modInstances.clear();
        }
        for (auto& pair : _npcCategories) {
            for (auto& instance : pair.second.instances) instance.modInstances.clear();
        }
        _specificNpcConfigs.clear();

        struct RawAnimationEntry {
            int playlistIndex;
            SubAnimationInstance subAnimInstance;
        };
        using StagingArea = std::map<std::string, std::map<int, std::map<std::string, std::vector<RawAnimationEntry>>>>;

        StagingArea playerStaging, npcStaging;
        std::map<RE::FormID, StagingArea> specificNpcStaging;

        const std::filesystem::path oarRootPath = "Data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer";
        if (!std::filesystem::exists(oarRootPath)) {
            SKSE::log::warn("Diretório do OAR não encontrado.");
            return;
        }

        for (const auto& entry : std::filesystem::recursive_directory_iterator(oarRootPath)) {
            if (entry.is_regular_file() && entry.path().filename() == "CycleMoveset.json") {
                std::ifstream ifs(entry.path());
                if (!ifs) continue;
                std::string jsonContent((std::istreambuf_iterator<char>(ifs)), {});
                ifs.close();
                rapidjson::Document doc;
                doc.Parse(jsonContent.c_str());
                if (doc.HasParseError() || !doc.IsArray()) continue;

                for (const auto& entity : doc.GetArray()) {
                    if (!entity.IsObject() || !entity.HasMember("FormID") || !entity.HasMember("Menu") ||
                        !entity.HasMember("Name"))
                        continue;

                    std::string actorFormIDStr = entity["FormID"].GetString();
                    const rapidjson::Value& menu = entity["Menu"];
                    if (!menu.IsArray()) continue;

                    StagingArea* targetStaging = nullptr;
                    if (actorFormIDStr == "Player") {
                        targetStaging = &playerStaging;
                    } else if (actorFormIDStr == "NPCs") {
                        targetStaging = &npcStaging;
                    } else {
                        try {
                            RE::FormID specificNpcId = std::stoul(actorFormIDStr, nullptr, 16);
                            // Pré-inicializa a entrada para armazenar o nome e plugin
                            _specificNpcConfigs[specificNpcId].name = entity["Name"].GetString();
                            if (entity.HasMember("Plugin")) {
                                _specificNpcConfigs[specificNpcId].pluginName = entity["Plugin"].GetString();
                            }
                            targetStaging = &specificNpcStaging[specificNpcId];
                        } catch (const std::exception&) {
                            continue;
                        }
                    }

                    if (!targetStaging) continue;

                    // O resto da lógica de leitura interna é a mesma
                    for (const auto& categoryJson : menu.GetArray()) {
                        if (!categoryJson.IsObject() || !categoryJson.HasMember("Category") ||
                            !categoryJson.HasMember("stances"))
                            continue;
                        std::string categoryName = categoryJson["Category"].GetString();
                        for (const auto& stanceJson : categoryJson["stances"].GetArray()) {
                            if (!stanceJson.IsObject() || !stanceJson.HasMember("index") ||
                                !stanceJson.HasMember("name") || !stanceJson.HasMember("animations"))
                                continue;
                            int stanceIndex = stanceJson["index"].GetInt();
                            if (stanceIndex < 1 || stanceIndex > 4) continue;
                            std::string movesetName = stanceJson["name"].GetString();
                            for (const auto& animJson : stanceJson["animations"].GetArray()) {
                                if (!animJson.IsObject() || !animJson.HasMember("index")) continue;
                                SubAnimationInstance newSubInstance;
                                auto subModIdxOpt = FindModIndexByName(animJson["sourceModName"].GetString());
                                if (subModIdxOpt) {
                                    newSubInstance.sourceModIndex = *subModIdxOpt;
                                    auto subAnimIdxOpt =
                                        FindSubAnimIndexByName(*subModIdxOpt, animJson["sourceSubName"].GetString());
                                    if (subAnimIdxOpt) {
                                        newSubInstance.sourceSubAnimIndex = *subAnimIdxOpt;
                                    } else {
                                        continue;
                                    }
                                } else {
                                    continue;
                                }
                                if (animJson.HasMember("pFront")) newSubInstance.pFront = animJson["pFront"].GetBool();
                                if (animJson.HasMember("pBack")) newSubInstance.pBack = animJson["pBack"].GetBool();
                                if (animJson.HasMember("pLeft")) newSubInstance.pLeft = animJson["pLeft"].GetBool();
                                if (animJson.HasMember("pRight")) newSubInstance.pRight = animJson["pRight"].GetBool();
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
                                if (animJson.HasMember("pDodge")) newSubInstance.pDodge = animJson["pDodge"].GetBool();
                                RawAnimationEntry rawEntry;
                                rawEntry.playlistIndex = animJson["index"].GetInt();
                                rawEntry.subAnimInstance = newSubInstance;
                                (*targetStaging)[categoryName][stanceIndex - 1][movesetName].push_back(rawEntry);
                            }
                        }
                    }
                }
            }
        }

        auto processStagedData = [&](StagingArea& staging, std::map<std::string, WeaponCategory>& targetCategories) {
            for (auto& catPair : staging) {
                auto categoryIt = targetCategories.find(catPair.first);
                if (categoryIt == targetCategories.end()) continue;
                for (auto& stancePair : catPair.second) {
                    int stanceIndex = stancePair.first;
                    CategoryInstance& targetInstance = categoryIt->second.instances[stanceIndex];
                    for (auto& movesetPair : stancePair.second) {
                        auto& rawEntries = movesetPair.second;
                        std::sort(rawEntries.begin(), rawEntries.end(),
                                  [](const RawAnimationEntry& a, const RawAnimationEntry& b) {
                                      return a.playlistIndex < b.playlistIndex;
                                  });
                        auto modIdxOpt = FindModIndexByName(movesetPair.first);
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
                        for (const auto& entry : rawEntries) {
                            modInstancePtr->subAnimationInstances.push_back(entry.subAnimInstance);
                        }
                    }
                }
            }
        };

        processStagedData(playerStaging, _categories);
        processStagedData(npcStaging, _npcCategories);

        std::map<std::string, WeaponCategory> cleanCategoriesTemplate;
        for (const auto& pair : _categories) {
            cleanCategoriesTemplate[pair.first] = pair.second;
            for (auto& instance : cleanCategoriesTemplate[pair.first].instances) {
                instance.modInstances.clear();
            }
        }

        for (auto& specificPair : specificNpcStaging) {
            RE::FormID npcID = specificPair.first;
            StagingArea& stagingData = specificPair.second;
            _specificNpcConfigs[npcID].categories = cleanCategoriesTemplate;
            processStagedData(stagingData, _specificNpcConfigs.at(npcID).categories);
        }

        SKSE::log::info("Carregamento e fusão de movesets concluído.");
        UpdateMaxMovesetCache();
    }
    // FIM DA SUBSTITUIÇÃO

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
            RE::DebugNotification("ERROR: Moveset name cannot be empty!");
            return;
        }

        std::vector<WeaponCategory*> selectedCategories;
        for (const auto& [name, isSelected] : _newMovesetCategorySelection) {
            if (isSelected) {
                auto it = _categories.find(name);
                if (it != _categories.end()) {
                    selectedCategories.push_back(&it->second);
                }
            }
        }

        if (selectedCategories.empty()) {
            RE::DebugNotification("ERROR: At least one weapon category must be selected!");
            return;
        }

        SKSE::log::info("Iniciando salvamento do moveset do usuário: {}", movesetName);
        const std::filesystem::path oarRootPath = "Data\\meshes\\actors\\character\\animations\\OpenAnimationReplacer";
        std::filesystem::path newMovesetPath = oarRootPath / movesetName;

        try {
            std::filesystem::create_directories(newMovesetPath);

            rapidjson::Document doc;
            doc.SetObject();
            auto& allocator = doc.GetAllocator();
            doc.AddMember("name", rapidjson::Value(movesetName.c_str(), allocator), allocator);
            doc.AddMember("author", rapidjson::Value(_newMovesetAuthor, allocator), allocator);
            doc.AddMember("description", rapidjson::Value(_newMovesetDesc, allocator), allocator);

            std::ofstream outFile(newMovesetPath / "config.json");
            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
            doc.Accept(writer);
            outFile << buffer.GetString();
        } catch (const std::filesystem::filesystem_error& e) {
            SKSE::log::error("Falha ao criar a pasta do moveset: {}. Erro: {}", newMovesetPath.string(), e.what());
            RE::DebugNotification("ERROR: Failed to create moveset folder!");
            return;
        }

        // NOVA ESTRUTURA PARA AGRUPAR DADOS DE SALVAMENTO
        struct SubmovesetSaveData {
            // Armazena todas as instâncias que compartilham o mesmo nome editado
            std::vector<const CreatorSubAnimationInstance*> instances;
            std::vector<FileSaveConfig> configs;
        };
        std::map<std::string, SubmovesetSaveData> uniqueSubmovesets;

        for (WeaponCategory* cat : selectedCategories) {
            if (_movesetCreatorStances.find(cat->name) == _movesetCreatorStances.end()) continue;

            auto& stances = _movesetCreatorStances.at(cat->name);
            for (int i = 0; i < 4; ++i) {
                if (!_newMovesetStanceEnabled[i]) continue;

                int playlistParentCounter = 1;
                int lastParentOrder = 0;

                for (const auto& subInst : stances[i].subMovesets) {
                    std::string subName = subInst.editedName.data();
                    if (subName.empty()) continue;

                    bool isParent = !(subInst.pFront || subInst.pBack || subInst.pLeft || subInst.pRight ||
                                      subInst.pFrontRight || subInst.pFrontLeft || subInst.pBackRight ||
                                      subInst.pBackLeft || subInst.pRandom || subInst.pDodge);

                    int order = isParent ? playlistParentCounter++ : lastParentOrder;
                    if (isParent) lastParentOrder = order;

                    FileSaveConfig config;
                    config.category = cat;
                    config.instance_index = i + 1;
                    config.isParent = isParent;
                    config.order_in_playlist = order;
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

                    uniqueSubmovesets[subName].configs.push_back(config);
                    // EM VEZ DE SOBRESCREVER, ADICIONA À LISTA
                    uniqueSubmovesets[subName].instances.push_back(&subInst);
                }
            }
        }

        for (const auto& pair : uniqueSubmovesets) {
            const std::string& subName = pair.first;
            const SubmovesetSaveData& data = pair.second;
            std::filesystem::path subMovesetPath = newMovesetPath / subName;

            try {
                std::filesystem::create_directory(subMovesetPath);
            } catch (...) {
                continue;
            }

            UpdateOrCreateJson(subMovesetPath / "config.json", data.configs);

            // LÓGICA ATUALIZADA PARA O CYCLEDAR.JSON
            {
                rapidjson::Document cycleDoc;
                cycleDoc.SetObject();
                auto& allocator = cycleDoc.GetAllocator();

                // CRIA O ARRAY DE FONTES
                rapidjson::Value sourcesArray(rapidjson::kArrayType);
                bool anyBfco = false;

                // Itera sobre todas as instâncias agrupadas sob este nome
                for (const auto* instancePtr : data.instances) {
                    if (!instancePtr || !instancePtr->sourceDef) continue;

                    rapidjson::Value sourceObj(rapidjson::kObjectType);

                    // Pega o caminho original da pasta
                    std::string originalPathStr;
                    if (instancePtr->sourceDef->path.filename() == "config.json") {
                        originalPathStr = instancePtr->sourceDef->path.parent_path().string();
                    } else {
                        originalPathStr = instancePtr->sourceDef->path.string();
                    }
                    size_t pos = originalPathStr.find("Data\\");
                    if (pos != std::string::npos) {
                        originalPathStr = originalPathStr.substr(pos + 5);
                    }
                    sourceObj.AddMember("path", rapidjson::Value(originalPathStr.c_str(), allocator), allocator);

                    // Adiciona a lista "filesToCopy" se houver alguma seleção
                    int selectedCount = 0;
                    rapidjson::Value filesArray(rapidjson::kArrayType);
                    for (const auto& [filename, isSelected] : instancePtr->hkxFileSelection) {
                        if (isSelected) {
                            filesArray.PushBack(rapidjson::Value(filename.c_str(), allocator), allocator);
                            selectedCount++;
                        }
                    }
                    if (selectedCount < instancePtr->hkxFileSelection.size() && selectedCount > 0) {
                        sourceObj.AddMember("filesToCopy", filesArray, allocator);
                    }
                    // Se nenhum arquivo for selecionado para esta fonte, pula ela.
                    if (selectedCount == 0) continue;

                    sourcesArray.PushBack(sourceObj, allocator);
                    if (instancePtr->isBFCO) anyBfco = true;
                }

                cycleDoc.AddMember("sources", sourcesArray, allocator);
                cycleDoc.AddMember("conversionDone", false, allocator);
                cycleDoc.AddMember("convertBFCO", anyBfco, allocator);

                std::ofstream outFile(subMovesetPath / "CycleDar.json");
                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
                cycleDoc.Accept(writer);
                outFile << buffer.GetString();
            }

            {
                rapidjson::Document cycleMovesetDoc;
                cycleMovesetDoc.SetArray();
                auto& allocator = cycleMovesetDoc.GetAllocator();

                rapidjson::Value playerObj(rapidjson::kObjectType);
                playerObj.AddMember("Name", "Player", allocator);
                // O FormID para Player é o próprio nome, para consistência com a lógica de carregamento
                playerObj.AddMember("FormID", "Player", allocator);
                rapidjson::Value menuArray(rapidjson::kArrayType);

                // Agrupa as configurações por categoria para este submoveset
                std::map<std::string, std::vector<const FileSaveConfig*>> configsByCategory;
                for (const auto& config : data.configs) {
                    configsByCategory[config.category->name].push_back(&config);
                }

                for (const auto& catPair : configsByCategory) {
                    rapidjson::Value categoryObj(rapidjson::kObjectType);
                    categoryObj.AddMember("Category", rapidjson::Value(catPair.first.c_str(), allocator), allocator);
                    rapidjson::Value stancesArray(rapidjson::kArrayType);

                    // Agrupa as configurações por stance
                    std::map<int, std::vector<const FileSaveConfig*>> configsByStance;
                    for (const auto* configPtr : catPair.second) {
                        configsByStance[configPtr->instance_index].push_back(configPtr);
                    }

                    for (const auto& stancePair : configsByStance) {
                        rapidjson::Value newStanceObj(rapidjson::kObjectType);
                        newStanceObj.AddMember("index", stancePair.first, allocator);
                        newStanceObj.AddMember("type", "moveset", allocator);
                        newStanceObj.AddMember("name", rapidjson::Value(movesetName.c_str(), allocator), allocator);

                        rapidjson::Value animationsArray(rapidjson::kArrayType);
                        for (const auto* configPtr : stancePair.second) {
                            rapidjson::Value animObj(rapidjson::kObjectType);
                            animObj.AddMember("index", configPtr->order_in_playlist, allocator);
                            animObj.AddMember("sourceModName", rapidjson::Value(movesetName.c_str(), allocator),
                                              allocator);
                            animObj.AddMember("sourceSubName", rapidjson::Value(subName.c_str(), allocator), allocator);
                            animObj.AddMember("sourceConfigPath",
                                              rapidjson::Value(subMovesetPath.string().c_str(), allocator), allocator);
                            animObj.AddMember("pFront", configPtr->pFront, allocator);
                            animObj.AddMember("pBack", configPtr->pBack, allocator);
                            animObj.AddMember("pLeft", configPtr->pLeft, allocator);
                            animObj.AddMember("pRight", configPtr->pRight, allocator);
                            animObj.AddMember("pFrontRight", configPtr->pFrontRight, allocator);
                            animObj.AddMember("pFrontLeft", configPtr->pFrontLeft, allocator);
                            animObj.AddMember("pBackRight", configPtr->pBackRight, allocator);
                            animObj.AddMember("pBackLeft", configPtr->pBackLeft, allocator);
                            animObj.AddMember("pRandom", configPtr->pRandom, allocator);
                            animObj.AddMember("pDodge", configPtr->pDodge, allocator);
                            animationsArray.PushBack(animObj, allocator);
                        }
                        newStanceObj.AddMember("animations", animationsArray, allocator);
                        stancesArray.PushBack(newStanceObj, allocator);
                    }
                    categoryObj.AddMember("stances", stancesArray, allocator);
                    menuArray.PushBack(categoryObj, allocator);
                }

                playerObj.AddMember("Menu", menuArray, allocator);
                cycleMovesetDoc.PushBack(playerObj, allocator);

                std::ofstream outFile(subMovesetPath / "CycleMoveset.json");
                rapidjson::StringBuffer buffer;
                rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
                cycleMovesetDoc.Accept(writer);
                outFile << buffer.GetString();
            }
            // A lógica para gerar o CycleMoveset.json (se você a mantiver) permanece a mesma,
            // pois ela já é baseada nas FileSaveConfig, que foram corretamente agrupadas.
        }

        _newMovesetCategorySelection.clear();
        _movesetCreatorStances.clear();

        SKSE::log::info("Salvamento do moveset '{}' concluído.", movesetName);
        RE::DebugNotification(std::format("Moveset '{}' salvo com sucesso!", movesetName).c_str());
        _showRestartPopup = true;
    }


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


    void AnimationManager::DrawAddDarModal() {
        if (_isAddDarModalOpen) {
            ImGui::OpenPopup("Add DAR animation");
            _isAddDarModalOpen = false;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 modal_list_size = ImVec2(viewport->Size.x * 0.5f, viewport->Size.y * 0.5f);
        ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal("Add DAR animation", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Library DAR");
            ImGui::Separator();
            static char darFilter[128] = "";
            ImGui::InputText(LOC("filter"), darFilter, sizeof(darFilter));
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
                        if (ImGui::Button(LOC("add"))) {
                            if (_stanceToAddTo) {
                                CreatorSubAnimationInstance newInstance;
                                // O ponteiro agora aponta para um elemento no nosso vetor _darSubMovesets
                                newInstance.sourceDef = &darSubDef;
                                strcpy_s(newInstance.editedName.data(), newInstance.editedName.size(),
                                         darSubDef.name.c_str());
                                PopulateHkxFiles(newInstance);
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
            if (ImGui::Button(LOC("close"), ImVec2(120, 0))) {
                strcpy_s(darFilter, "");
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // Função para dividir uma string de keywords separadas por vírgula em um vetor
    std::vector<std::string> SplitKeywords(const std::string& s) {
        std::vector<std::string> tokens;
        std::string token;
        std::istringstream tokenStream(s);
        while (std::getline(tokenStream, token, ',')) {
            // Remove espaços em branco antes e depois do token
            token.erase(0, token.find_first_not_of(" \t\n\r"));
            token.erase(token.find_last_not_of(" \t\n\r") + 1);
            if (!token.empty()) {
                tokens.push_back(token);
            }
        }
        return tokens;
    }

    void AnimationManager::SaveCustomCategories() {
        const std::filesystem::path categoriesPath = "Data/SKSE/Plugins/CycleMovesets/Categories";

        // 1. Garante que o diretório de salvamento existe
        try {
            if (!std::filesystem::exists(categoriesPath)) {
                std::filesystem::create_directories(categoriesPath);
            }
        } catch (const std::filesystem::filesystem_error& e) {
            SKSE::log::error("Falha ao criar o diretório de categorias: {}. Erro: {}", categoriesPath.string(), e.what());
            return;
        }

        SKSE::log::info("Salvando categorias customizadas em arquivos individuais...");
        std::set<std::filesystem::path> savedFilePaths;

        if (std::filesystem::exists(categoriesPath)) {
            for (const auto& entry : std::filesystem::directory_iterator(categoriesPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".json") {
                    savedFilePaths.insert(entry.path());
                }
            }
        }

        // 2. Salva cada categoria customizada em seu próprio arquivo
        for (const auto& pair : _categories) {
            const WeaponCategory& category = pair.second;
            if (category.isCustom) {
                rapidjson::Document doc;
                doc.SetObject();  // O arquivo conterá um único objeto
                auto& allocator = doc.GetAllocator();

                // Popula o objeto JSON com os dados da categoria
                doc.AddMember("name", rapidjson::Value(category.name.c_str(), allocator), allocator);
                doc.AddMember("baseCategoryName", rapidjson::Value(category.baseCategoryName.c_str(), allocator),
                              allocator);
                doc.AddMember("isDualWield", category.isDualWield, allocator);
                doc.AddMember("isShieldCategory", category.isShieldCategory, allocator);

                rapidjson::Value keywordsArray(rapidjson::kArrayType);
                for (const auto& kw : category.keywords) {
                    keywordsArray.PushBack(rapidjson::Value(kw.c_str(), allocator), allocator);
                }
                doc.AddMember("keywords", keywordsArray, allocator);

                if (category.isDualWield) {
                    std::string leftHandBaseName = "Unarmed";  // Padrão
                    for (const auto& basePair : _categories) {
                        if (!basePair.second.isCustom &&
                            basePair.second.equippedTypeValue == category.leftHandEquippedTypeValue &&
                            basePair.second.leftHandEquippedTypeValue == category.leftHandEquippedTypeValue) {
                            leftHandBaseName = basePair.second.name;
                            break;
                        }
                    }
                    doc.AddMember("leftHandBaseCategoryName", rapidjson::Value(leftHandBaseName.c_str(), allocator),
                                  allocator);

                    rapidjson::Value leftKeywordsArray(rapidjson::kArrayType);
                    for (const auto& kw : category.leftHandKeywords) {
                        leftKeywordsArray.PushBack(rapidjson::Value(kw.c_str(), allocator), allocator);
                    }
                    doc.AddMember("leftHandKeywords", leftKeywordsArray, allocator);
                }

                // Define o caminho do arquivo e o salva
                std::filesystem::path categoryFilePath = categoriesPath / (category.name + ".json");
                std::ofstream ofs(categoryFilePath);
                if (ofs) {
                    rapidjson::StringBuffer buffer;
                    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
                    doc.Accept(writer);
                    ofs << buffer.GetString();
                    ofs.close();
                    savedFilePaths.insert(categoryFilePath);  // Adiciona à lista de arquivos válidos
                } else {
                    SKSE::log::error("Falha ao abrir {} para escrita!", categoryFilePath.string());
                }
            }
        }
        std::set<std::filesystem::path> currentCustomCategoryFiles;
        for (const auto& pair : _categories) {
            if (pair.second.isCustom) {
                currentCustomCategoryFiles.insert(categoriesPath / (pair.first + ".json"));
            }
        }
        // 3. Remove arquivos órfãos (de categorias que foram deletadas na UI)
        for (const auto& existingPath : savedFilePaths) {
            if (currentCustomCategoryFiles.find(existingPath) == currentCustomCategoryFiles.end()) {
                SKSE::log::info("Removendo arquivo de categoria órfão: {}", existingPath.string());
                std::filesystem::remove(existingPath);
            }
        }
    }

    void AnimationManager::LoadCustomCategories() {
        const std::filesystem::path categoriesPath = "Data/SKSE/Plugins/CycleMovesets/Categories";
        if (!std::filesystem::exists(categoriesPath)) {
            SKSE::log::info("Diretório de categorias customizadas não encontrado. Pulando.");
            return;
        }

        SKSE::log::info("Carregando categorias customizadas de arquivos individuais...");

        std::map<std::string, const WeaponCategory*> baseCategories;
        for (const auto& pair : _categories) {
            if (!pair.second.isCustom) {
                baseCategories[pair.second.name] = &pair.second;
            }
        }

        // Itera sobre cada arquivo no diretório de categorias
        for (const auto& entry : std::filesystem::directory_iterator(categoriesPath)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".json") {
                continue;
            }

            std::ifstream ifs(entry.path());
            if (!ifs) {
                SKSE::log::error("Falha ao abrir o arquivo de categoria: {}", entry.path().string());
                continue;
            }

            std::string jsonContent((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
            ifs.close();

            rapidjson::Document doc;
            doc.Parse(jsonContent.c_str());

            if (doc.HasParseError() || !doc.IsObject()) {
                SKSE::log::error("Erro no parse do JSON ou o arquivo não é um objeto para: {}", entry.path().string());
                continue;
            }

            const rapidjson::Value& categoryObj = doc;  // O documento raiz é o próprio objeto

            // O resto da lógica de leitura é a mesma, mas usando 'categoryObj'
            if (!categoryObj.HasMember("name") || !categoryObj["name"].IsString() ||
                !categoryObj.HasMember("baseCategoryName") || !categoryObj["baseCategoryName"].IsString() ||
                !categoryObj.HasMember("isDualWield") || !categoryObj["isDualWield"].IsBool() ||
                !categoryObj.HasMember("keywords") || !categoryObj["keywords"].IsArray()) {
                SKSE::log::warn("Objeto de categoria customizada malformado ou com campos faltando em {}. Pulando.",
                                entry.path().string());
                continue;
            }

            std::string name = categoryObj["name"].GetString();
            std::string baseName = categoryObj["baseCategoryName"].GetString();

            auto it = baseCategories.find(baseName);
            if (it == baseCategories.end()) {
                SKSE::log::warn("Categoria base '{}' para '{}' não encontrada. Pulando.", baseName, name);
                continue;
            }
            const WeaponCategory* baseCat = it->second;

            WeaponCategory newCat;
            newCat.name = name;
            newCat.isCustom = true;
            newCat.baseCategoryName = baseName;
            newCat.equippedTypeValue = baseCat->equippedTypeValue;
            newCat.isDualWield = categoryObj["isDualWield"].GetBool();
            if (categoryObj.HasMember("isShieldCategory") && categoryObj["isShieldCategory"].IsBool()) {
                newCat.isShieldCategory = categoryObj["isShieldCategory"].GetBool();
            } else {
                newCat.isShieldCategory = false;  // Valor padrão se não existir no arquivo
            }

            for (const auto& kw : categoryObj["keywords"].GetArray()) {
                if (kw.IsString()) newCat.keywords.push_back(kw.GetString());
            }

            if (newCat.isDualWield) {
                if (!categoryObj.HasMember("leftHandBaseCategoryName") ||
                    !categoryObj["leftHandBaseCategoryName"].IsString() || !categoryObj.HasMember("leftHandKeywords") ||
                    !categoryObj["leftHandKeywords"].IsArray()) {
                    SKSE::log::warn("Categoria dual '{}' não tem campos de mão esquerda. Pulando.", name);
                    continue;
                }
                std::string leftHandBaseName = categoryObj["leftHandBaseCategoryName"].GetString();
                auto itLeft = baseCategories.find(leftHandBaseName);
                if (itLeft != baseCategories.end()) {
                    newCat.leftHandEquippedTypeValue = itLeft->second->equippedTypeValue;
                } else {
                    newCat.leftHandEquippedTypeValue = 0.0;
                }
                for (const auto& kw : categoryObj["leftHandKeywords"].GetArray()) {
                    if (kw.IsString()) newCat.leftHandKeywords.push_back(kw.GetString());
                }
            } else {
                newCat.leftHandEquippedTypeValue = baseCat->leftHandEquippedTypeValue;
            }

            for (int i = 0; i < 4; ++i) {
                std::string defaultName = std::format("Stance {}", i + 1);
                newCat.stanceNames[i] = defaultName;
                strcpy_s(newCat.stanceNameBuffers[i].data(), newCat.stanceNameBuffers[i].size(), defaultName.c_str());
            }

            _categories[newCat.name] = newCat;
        }
    }

    void AnimationManager::DrawCreateCategoryModal() {
        // Determina se estamos no modo de edição baseado no ponteiro
        bool isEditing = (_categoryToEditPtr != nullptr);
        const char* popupTitle = isEditing ? "Edit Custom Category" : "Create New Category";

        if (_isCreateCategoryModalOpen) {
            ImGui::OpenPopup(popupTitle);
            _isCreateCategoryModalOpen = false;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

        if (ImGui::BeginPopupModal(popupTitle, NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            // --- Listas de categorias base para os combos ---
            std::vector<const char*> baseCategoryNames;
            std::vector<const WeaponCategory*> baseCategoryPtrs;
            for (const auto& pair : _categories) {
                if (!pair.second.isCustom && !pair.second.isDualWield && !pair.second.isShieldCategory) {
                    baseCategoryNames.push_back(pair.first.c_str());
                    baseCategoryPtrs.push_back(&pair.second);
                }
            }
            std::vector<const char*> dualCategoryNames;
            std::vector<const WeaponCategory*> dualCategoryPtrs;
            for (const auto& pair : _categories) {
                if (!pair.second.isCustom) {
                    dualCategoryNames.push_back(pair.first.c_str());
                    dualCategoryPtrs.push_back(&pair.second);
                }
            }

            // --- Campos do formulário ---
            ImGui::InputText("Category Name", _newCategoryNameBuffer, sizeof(_newCategoryNameBuffer));

            // CORREÇÃO PONTO 1: O combo da arma base agora aparece mesmo se "Shield" estiver marcado
            ImGui::Combo("Base Weapon (Right Hand)", &_newCategoryBaseIndex, baseCategoryNames.data(),
                         baseCategoryNames.size());

            ImGui::InputText("Keywords (comma-separated)", _newCategoryKeywordsBuffer, sizeof(_newCategoryKeywordsBuffer));

            if (ImGui::Checkbox("Is Dual Wield", &_newCategoryIsDual)) {
                if (_newCategoryIsDual) _newCategoryIsShield = false;
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("Left Hand is Shield", &_newCategoryIsShield)) {
                if (_newCategoryIsShield) _newCategoryIsDual = false;
            }

            if (_newCategoryIsDual) {
                ImGui::Separator();
                ImGui::Text("Dual Wield Options");
                ImGui::Combo("Base Weapon (Left Hand)", &_newCategoryLeftHandBaseIndex, dualCategoryNames.data(),
                             dualCategoryNames.size());
                ImGui::InputText("Left Hand Keywords", _newCategoryLeftHandKeywordsBuffer,
                                 sizeof(_newCategoryLeftHandKeywordsBuffer));
            }
            ImGui::Separator();

            // --- Lógica de Salvamento (Unificada) ---
            const char* saveButtonText = LOC("save");
            if (ImGui::Button(saveButtonText, ImVec2(120, 0))) {
                std::string newName = _newCategoryNameBuffer;
                std::string originalName = isEditing ? _categoryToEditPtr->name : "";

                if (newName.empty() || (newName != originalName && _categories.count(newName))) {
                    RE::DebugNotification("ERROR: Category name cannot be empty or already exists!");
                } else {
                    // Se o nome mudou, remove a categoria antiga para recriá-la
                    if (isEditing && newName != originalName) {
                        _categories.erase(originalName);
                        _npcCategories.erase(originalName);
                    }

                    WeaponCategory& catToUpdate = _categories[newName];  // Cria ou acessa a categoria
                    catToUpdate.name = newName;
                    catToUpdate.isCustom = true;
                    catToUpdate.isDualWield = _newCategoryIsDual;
                    catToUpdate.isShieldCategory = _newCategoryIsShield;

                    // CORREÇÃO PONTO 1 & 2: Lógica de atribuição de dados
                    const WeaponCategory* baseCat = baseCategoryPtrs[_newCategoryBaseIndex];
                    catToUpdate.baseCategoryName = baseCat->name;
                    catToUpdate.keywords = SplitKeywords(_newCategoryKeywordsBuffer);  // Sempre salva keywords

                    if (catToUpdate.isShieldCategory) {
                        catToUpdate.equippedTypeValue = baseCat->equippedTypeValue;  // Usa o tipo da arma selecionada
                        catToUpdate.leftHandEquippedTypeValue = 11.0;                // Valor fixo para escudo
                    } else if (catToUpdate.isDualWield) {
                        const WeaponCategory* leftBaseCat = dualCategoryPtrs[_newCategoryLeftHandBaseIndex];
                        catToUpdate.equippedTypeValue = baseCat->equippedTypeValue;
                        catToUpdate.leftHandEquippedTypeValue = leftBaseCat->equippedTypeValue;
                        catToUpdate.leftHandKeywords = SplitKeywords(_newCategoryLeftHandKeywordsBuffer);
                    } else {  // Categoria normal de uma mão
                        catToUpdate.equippedTypeValue = baseCat->equippedTypeValue;
                        catToUpdate.leftHandEquippedTypeValue = baseCat->leftHandEquippedTypeValue;
                    }

                    // Inicializa stances para a nova/editada categoria
                    if (!isEditing) {
                        for (int i = 0; i < 4; ++i) {
                            std::string defaultName = std::format("Stance {}", i + 1);
                            catToUpdate.stanceNames[i] = defaultName;
                            strcpy_s(catToUpdate.stanceNameBuffers[i].data(), catToUpdate.stanceNameBuffers[i].size(),
                                     defaultName.c_str());
                        }
                    }

                    _npcCategories[newName] = catToUpdate;  // Sincroniza com a lista de NPCs
                    _categoryToEditPtr = nullptr;           // Reseta o ponteiro de edição
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(LOC("close"), ImVec2(120, 0))) {
                _categoryToEditPtr = nullptr;  // Reseta o ponteiro de edição
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        } else {
            // Garante que o ponteiro de edição seja limpo se o popup for fechado de outra forma
            if (isEditing) {
                _categoryToEditPtr = nullptr;
            }
        }
    }

    void AnimationManager::DrawCategoryManager() {
        if (ImGui::Button("Create New Category")) {
            _categoryToEditPtr = nullptr;  // Garante que estamos no modo de criação
            // Limpa os buffers para um formulário novo
            strcpy_s(_originalCategoryName, "");
            strcpy_s(_newCategoryNameBuffer, "");
            strcpy_s(_newCategoryKeywordsBuffer, "");
            strcpy_s(_newCategoryLeftHandKeywordsBuffer, "");
            _newCategoryBaseIndex = 0;
            _newCategoryLeftHandBaseIndex = 0;
            _newCategoryIsDual = false;
            _newCategoryIsShield = false;
            _isCreateCategoryModalOpen = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Create new weapon categories based on vanilla types, but with specific keywords.");
        }

        ImGui::Separator();

        if (ImGui::BeginTable("CategoriesTable", 3,
                              ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Category Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Details", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableHeadersRow();

            std::string categoryToDelete;

            // Usamos um iterador para poder apagar da lista de forma segura
            for (auto it = _categories.begin(); it != _categories.end();) {
                auto& [name, category] = *it;

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s", name.c_str());

                ImGui::TableNextColumn();
                if (category.isCustom) {
                    ImGui::Text("Base: %s", category.baseCategoryName.c_str());
                } else {
                    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Base Category");
                }

                ImGui::TableNextColumn();
                if (category.isCustom) {
                    ImGui::PushID(name.c_str());
                    if (ImGui::Button("Edit")) {
                        _categoryToEditPtr = &category;  // Aponta para a categoria, ativando o modo de edição

                        // Preenche os buffers com os dados atuais da categoria
                        strcpy_s(_originalCategoryName, name.c_str());  // Guarda o nome original
                        strcpy_s(_newCategoryNameBuffer, name.c_str());
                        _newCategoryIsDual = category.isDualWield;
                        _newCategoryIsShield = category.isShieldCategory;

                        // Converte vetores de keywords para strings
                        auto join_keywords = [](const std::vector<std::string>& keywords) {
                            std::string result;
                            for (size_t i = 0; i < keywords.size(); ++i) {
                                result += keywords[i] + (i == keywords.size() - 1 ? "" : ", ");
                            }
                            return result;
                        };
                        strcpy_s(_newCategoryKeywordsBuffer, join_keywords(category.keywords).c_str());
                        strcpy_s(_newCategoryLeftHandKeywordsBuffer, join_keywords(category.leftHandKeywords).c_str());

                        // --- INÍCIO DA CORREÇÃO #3: ENCONTRAR ÍNDICES CORRETOS ---
                        // Lógica para encontrar o índice da base direita
                        _newCategoryBaseIndex = 0;
                        int current_idx = 0;
                        for (const auto& pair : _categories) {
                            if (!pair.second.isCustom && !pair.second.isDualWield) {
                                if (pair.first == category.baseCategoryName) {
                                    _newCategoryBaseIndex = current_idx;
                                    break;
                                }
                                current_idx++;
                            }
                        }

                        // Lógica para encontrar o índice da base esquerda (se for dual wield)
                        _newCategoryLeftHandBaseIndex = 0;
                        if (category.isDualWield) {
                            current_idx = 0;
                            for (const auto& pair : _categories) {
                                if (!pair.second.isCustom) {
                                    // Precisa encontrar o nome da categoria base da mão esquerda
                                    // Esta lógica é complexa, por enquanto vamos procurar pelo typeValue
                                    if (pair.second.equippedTypeValue == category.leftHandEquippedTypeValue &&
                                        pair.second.leftHandEquippedTypeValue == category.leftHandEquippedTypeValue) {
                                        _newCategoryLeftHandBaseIndex = current_idx;
                                        break;
                                    }
                                    current_idx++;
                                }
                            }
                        }
                        // --- FIM DA CORREÇÃO #3 ---

                        _isCreateCategoryModalOpen = true;  // Abre o mesmo modal, mas agora em modo de edição
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Delete")) {
                        categoryToDelete = name;
                    }
                    ImGui::PopID();
                }
                ++it;
            }
            ImGui::EndTable();

            if (!categoryToDelete.empty()) {
                _categories.erase(categoryToDelete);
                _npcCategories.erase(categoryToDelete);
                SKSE::log::info("Categoria '{}' removida.", categoryToDelete);
            }
        }
    }

    void AnimationManager::AddCompareEquippedTypeCondition(rapidjson::Value& conditionsArray, double type, bool isLeftHand,
                                                           rapidjson::Document::AllocatorType& allocator) {
        rapidjson::Value equippedType(rapidjson::kObjectType);
        equippedType.AddMember("condition", "IsEquippedType", allocator);
        rapidjson::Value typeVal(rapidjson::kObjectType);
        typeVal.AddMember("value", type, allocator);
        equippedType.AddMember("Type", typeVal, allocator);
        equippedType.AddMember("Left hand", isLeftHand, allocator);
        conditionsArray.PushBack(equippedType, allocator);
    }

    void AnimationManager::AddShieldCategoryExclusions(rapidjson::Value& parentArray,
                                                       rapidjson::Document::AllocatorType& allocator) {
        // 1. Coleta todas as keywords de categorias de escudo customizadas
        std::vector<std::string> competingKeywords;
        for (const auto& pair : _categories) {
            const WeaponCategory& otherCategory = pair.second;
            // A condição é: ser customizada, ser uma categoria de escudo, E ter keywords
            if (otherCategory.isCustom && otherCategory.isShieldCategory && !otherCategory.keywords.empty()) {
                competingKeywords.insert(competingKeywords.end(), otherCategory.keywords.begin(),
                                         otherCategory.keywords.end());
            }
        }

        if (competingKeywords.empty()) {
            return;  // Nenhuma exclusão necessária
        }

        // 2. Cria um único bloco AND para conter todas as exclusões (NOT keyword1 AND NOT keyword2 ...)
        rapidjson::Value exclusionAndBlock(rapidjson::kObjectType);
        exclusionAndBlock.AddMember("condition", "AND", allocator);
        exclusionAndBlock.AddMember("comment", "Exclude competing custom Shield + Weapon categories", allocator);
        rapidjson::Value innerExclusionConditions(rapidjson::kArrayType);

        for (const auto& keyword : competingKeywords) {
            // Adiciona a condição 'IsEquippedHasKeyword' negada para a mão direita (onde a arma com keyword está)
            AddKeywordCondition(innerExclusionConditions, keyword, false, true, allocator);
        }

        exclusionAndBlock.AddMember("Conditions", innerExclusionConditions, allocator);
        parentArray.PushBack(exclusionAndBlock, allocator);
    }

    void AnimationManager::PopulateNpcList() {
        SKSE::log::info("Iniciando escaneamento de todos os NPCs...");
        _fullNpcList.clear();
        _pluginList.clear();
        std::set<std::string> uniquePlugins;

        auto dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            SKSE::log::error("Falha ao obter o TESDataHandler.");
            return;
        }

        const auto& npcArray = dataHandler->GetFormArray<RE::TESNPC>();
        for (const auto& npc : npcArray) {
            if (npc && !npc->IsPlayer() && npc->GetFile(0)) {
                NPCInfo info;
                info.formID = npc->GetFormID();

                // --- INÍCIO DA ALTERAÇÃO ---
                // Substituído npc->GetFormEditorID() pela chamada da clib_util para maior robustez.
                info.editorID = clib_util::editorID::get_editorID(npc);
                // --- FIM DA ALTERAÇÃO ---

                info.name = npc->GetName();

                auto plugin = npc->GetFile(0)->GetFilename();
                info.pluginName = std::string(plugin);

                _fullNpcList.push_back(info);
                uniquePlugins.insert(info.pluginName);
            }
        }

        _pluginList.push_back(LOC("all"));
        for (const auto& pluginName : uniquePlugins) {
            _pluginList.push_back(pluginName);
        }

        _npcListPopulated = true;
        SKSE::log::info("Escaneamento concluído. {} NPCs carregados de {} plugins.", _fullNpcList.size(),
                        uniquePlugins.size());
    }

    

    void AnimationManager::DrawNpcSelectionModal() {
        if (_isNpcSelectionModalOpen) {
            ImGui::OpenPopup(LOC("select_npc"));
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 center = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(viewport->Size.x * 0.7f, viewport->Size.y * 0.7f));

        if (ImGui::BeginPopupModal(LOC("select_npc"), &_isNpcSelectionModalOpen, ImGuiWindowFlags_None)) {
            if (!_npcListPopulated) {
                PopulateNpcList();
            }

            ImGui::InputText(LOC("filter"), _npcFilterBuffer, sizeof(_npcFilterBuffer));
            ImGui::SameLine();
            std::vector<const char*> pluginNamesCStr;
            for (const auto& name : _pluginList) {
                pluginNamesCStr.push_back(name.c_str());
            }
            ImGui::PushItemWidth(200);
            ImGui::Combo("Plugin", &_selectedPluginIndex, pluginNamesCStr.data(), pluginNamesCStr.size());
            ImGui::PopItemWidth();
            ImGui::Separator();

            if (ImGui::BeginTable("NpcListTable", 4,
                                  ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("EditorID", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("FormID", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn(LOC("enable_disable"), ImGuiTableColumnFlags_WidthFixed, 200.0f);
                ImGui::TableHeadersRow();

                std::string filterText = _npcFilterBuffer;
                std::transform(filterText.begin(), filterText.end(), filterText.begin(), ::tolower);
                std::string selectedPlugin = _pluginList[_selectedPluginIndex];

                for (const auto& npc : _fullNpcList) {
                    if (_selectedPluginIndex != 0 && npc.pluginName != selectedPlugin) continue;
                    std::string formIdStr = std::format("{:08X}", npc.formID);
                    std::string nameLower = npc.name;
                    std::string editorIdLower = npc.editorID;
                    std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    std::transform(editorIdLower.begin(), editorIdLower.end(), editorIdLower.begin(), ::tolower);
                    if (!filterText.empty() && nameLower.find(filterText) == std::string::npos &&
                        editorIdLower.find(filterText) == std::string::npos &&
                        formIdStr.find(filterText) == std::string::npos) {
                        continue;
                    }

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", npc.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", npc.editorID.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%s", formIdStr.c_str());
                    ImGui::TableNextColumn();

                    bool isConfigured = _specificNpcConfigs.count(npc.formID);
                    ImGui::PushID(npc.formID);
                    if (ImGui::Checkbox("##configured", &isConfigured)) {
                        if (isConfigured) {
                            // Lógica de criação de um novo NPC específico
                            SpecificNpcConfig newConfig;
                            newConfig.name = npc.name;
                            newConfig.pluginName = npc.pluginName;

                            // Cria um template de categorias limpas para o novo NPC
                            std::map<std::string, WeaponCategory> cleanCategoriesTemplate;
                            for (const auto& pair : _categories) {
                                cleanCategoriesTemplate[pair.first] = pair.second;
                                for (auto& instance : cleanCategoriesTemplate[pair.first].instances) {
                                    instance.modInstances.clear();
                                }
                            }
                            newConfig.categories = cleanCategoriesTemplate;

                            _specificNpcConfigs[npc.formID] = newConfig;
                            SKSE::log::info("NPC Específico adicionado: {} ({:08X})", npc.name, npc.formID);
                        } else {
                            _specificNpcConfigs.erase(npc.formID);
                            if (_currentlySelectedNpcFormID == npc.formID) {
                                _currentlySelectedNpcFormID = 0;
                            }
                            SKSE::log::info("Configuração do NPC Específico removida: {} ({:08X})", npc.name,
                                            npc.formID);
                        }
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            /*ImGui::Separator();
            if (ImGui::Button("Fechar", ImVec2(120, 0))) {
                _isNpcSelectionModalOpen = false;
            }*/
            ImGui::EndPopup();
        }
    }

    void AnimationManager::PopulateHkxFiles(CreatorSubAnimationInstance& instance) {
        if (!instance.sourceDef) return;

        // Garante que o caminho é um diretório
        std::filesystem::path sourceDirectory = instance.sourceDef->path;
        if (std::filesystem::is_regular_file(sourceDirectory)) {
            sourceDirectory = sourceDirectory.parent_path();
        }

        if (!std::filesystem::exists(sourceDirectory) || !std::filesystem::is_directory(sourceDirectory)) {
            return;
        }

        instance.hkxFileSelection.clear();
        for (const auto& fileEntry : std::filesystem::directory_iterator(sourceDirectory)) {
            if (fileEntry.is_regular_file()) {
                std::string extension = fileEntry.path().extension().string();
                std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
                if (extension == ".hkx") {
                    // Adiciona o arquivo à lista, selecionado por padrão
                    instance.hkxFileSelection[fileEntry.path().filename().string()] = true;
                }
            }
        }
    }