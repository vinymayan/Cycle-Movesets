#include "Serialization.h"
#include "Utils.h"
#include "Events.h"
#include <random>


// Scancodes das teclas WASD
constexpr uint32_t W_KEY = 0x11;
constexpr uint32_t A_KEY = 0x1E;
constexpr uint32_t S_KEY = 0x1F;
constexpr uint32_t D_KEY = 0x20;
int GlobalControl::g_directionalState = 0;
// Esta fun��o � chamada a cada frame de input
RE::BSEventNotifyControl InputListener::ProcessEvent(RE::InputEvent* const* a_event,
                                                     RE::BSTEventSource<RE::InputEvent*>*) {
    if (!a_event || !*a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    bool umaTeclaDeMovimentoMudou = false;

    for (auto* event = *a_event; event; event = event->next) {
        RE::INPUT_DEVICE device = event->GetDevice();
        // Ignora movimentos do mouse para n�o trocar o dispositivo acidentalmente
        if (device != RE::INPUT_DEVICE::kMouse && device != RE::INPUT_DEVICE::kNone) {
            if (lastUsedDevice != device) {
                lastUsedDevice = device;
                SKSE::log::info("Input device switched to: {}", (int)device);
                // Quando o dispositivo muda, precisamos re-registrar as hotkeys com a API
                GlobalControl::UpdateRegisteredHotkeys();
            }
        }
        // --- L�GICA DE MOVIMENTO (TECLADO E CONTROLE) ---
        if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kThumbstick) {
            auto* thumbstick = event->AsThumbstickEvent();
            if (thumbstick && thumbstick->IsLeft()) {
                // Normalizamos os valores para evitar pequenas flutua��es do anal�gico
                bool new_c_up = thumbstick->yValue > 0.5f;
                bool new_c_down = thumbstick->yValue < -0.5f;
                bool new_c_left = thumbstick->xValue < -0.5f;
                bool new_c_right = thumbstick->xValue > 0.5f;

                if (c_up != new_c_up || c_down != new_c_down || c_left != new_c_left || c_right != new_c_right) {
                    c_up = new_c_up;
                    c_down = new_c_down;
                    c_left = new_c_left;
                    c_right = new_c_right;
                    umaTeclaDeMovimentoMudou = true;
                }
            }
        } else if (event->GetEventType() == RE::INPUT_EVENT_TYPE::kButton) {
            auto* button = event->AsButtonEvent();
            if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;  // Ignora se n�o for um bot�o ou n�o for do teclado
            }

            const uint32_t scanCode = button->GetIDCode();

            // L�gica rigorosa de m�quina de estados para cada tecla
            if (scanCode == Settings::keyForward) {
                // S� mude para 'pressionado' se a tecla ESTIVER 'down' E nosso estado atual for 'solto'.
                if (button->IsDown() && !w_pressed) {
                    w_pressed = true;
                    umaTeclaDeMovimentoMudou = true;
                }
                // S� mude para 'solto' se a tecla ESTIVER 'up' E nosso estado atual for 'pressionado'.
                else if (button->IsUp() && w_pressed) {
                    w_pressed = false;
                    umaTeclaDeMovimentoMudou = true;
                }
            } else if (scanCode == Settings::keyLeft) {
                if (button->IsDown() && !a_pressed) {
                    a_pressed = true;
                    umaTeclaDeMovimentoMudou = true;
                } else if (button->IsUp() && a_pressed) {
                    a_pressed = false;
                    umaTeclaDeMovimentoMudou = true;
                }
            } else if (scanCode == Settings::keyBack) {
                if (button->IsDown() && !s_pressed) {
                    s_pressed = true;
                    umaTeclaDeMovimentoMudou = true;
                } else if (button->IsUp() && s_pressed) {
                    s_pressed = false;
                    umaTeclaDeMovimentoMudou = true;
                }
            } else if (scanCode == Settings::keyRight) {
                if (button->IsDown() && !d_pressed) {
                    d_pressed = true;
                    umaTeclaDeMovimentoMudou = true;
                } else if (button->IsUp() && d_pressed) {
                    d_pressed = false;
                    umaTeclaDeMovimentoMudou = true;
                }
            }
        }
    }

    // Apenas recalcule a dire��o se uma das nossas teclas de movimento REALMENTE mudou de estado.
    if (umaTeclaDeMovimentoMudou) {
        UpdateDirectionalState();
    }

    return RE::BSEventNotifyControl::kContinue;
}

// Esta fun��o calcula o valor final da sua vari�vel
void InputListener::UpdateDirectionalState() {
    //static int DirecionalCycleMoveset = 0;
    int VariavelAnterior = directionalState;
    
    

    // Prioriza o input do teclado. Se qualquer tecla WASD estiver pressionada, ignore o controle.
    // Caso contr�rio, use o estado do controle.
    bool FRENTE = w_pressed || (!w_pressed && !a_pressed && !s_pressed && !d_pressed && c_up);
    bool TRAS = s_pressed || (!w_pressed && !a_pressed && !s_pressed && !d_pressed && c_down);
    bool ESQUERDA = a_pressed || (!w_pressed && !a_pressed && !s_pressed && !d_pressed && c_left);
    bool DIREITA = d_pressed || (!w_pressed && !a_pressed && !s_pressed && !d_pressed && c_right);

    // A l�gica de decis�o permanece a mesma, mas agora usa as vari�veis combinadas
    if (FRENTE && ESQUERDA) {
        directionalState  = 8;  // Noroeste
    } else if (FRENTE && DIREITA) {
        directionalState  = 2;  // Nordeste
    } else if (TRAS && ESQUERDA) {
        directionalState  = 6;  // Sudoeste
    } else if (TRAS && DIREITA) {
        directionalState  = 4;  // Sudeste
    } else if (FRENTE) {
        directionalState  = 1;  // Norte (Frente)
    } else if (ESQUERDA) {
        directionalState  = 7;  // Oeste (Esquerda)
    } else if (TRAS) {
        directionalState  = 5;  // Sul (Tr�s)
    } else if (DIREITA) {
        directionalState  = 3;  // Leste (Direita)
    } else {
        directionalState  = 0;  // Parado
    }

    // Opcional: s� imprime no log se o valor mudar, para n�o poluir o log.
    if (VariavelAnterior != directionalState ) {
        //SKSE::log::info("DirecionalCycleMoveset  alterado para: {}", directionalState );
        GlobalControl::UpdateSkyPromptTexts();
        // Aqui voc� enviaria o valor para sua anima��o, por exemplo:
        // RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("MinhaVariavelDirecional",
        // directionalState );
        if (GlobalControl::g_isWeaponDrawn && !GlobalControl::MovesetChangesOpen && !GlobalControl::StanceChangesOpen && GlobalControl::IsThirdPerson()) {
            SkyPromptAPI::SendPrompt(GlobalControl::MovesetSink::GetSingleton(), GlobalControl::g_clientID);
            //SKSE::log::info("SkyPrompt reenviado devido � mudan�a de dire��o.");
            
        }
        if (GlobalControl::g_isWeaponDrawn && GlobalControl::MovesetChangesOpen && !GlobalControl::StanceChangesOpen &&
            GlobalControl::IsThirdPerson()) {
            SkyPromptAPI::SendPrompt(GlobalControl::MovesetChangesSink::GetSingleton(), GlobalControl::g_clientID);
            
            //SKSE::log::info("SkyPrompt reenviado devido � mudan�a de dire��o e menu aberto.");
        }
    }
    RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("DirecionalCycleMoveset", directionalState);

}

// NOVA FUN��O AUXILIAR PARA QUALQUER ATOR
std::string GetActorWeaponCategoryName(RE::Actor* targetActor) {
    if (!targetActor) return "Unarmed";

    auto rightHand = targetActor->GetEquippedObject(false);
    auto leftHand = targetActor->GetEquippedObject(true);

    RE::TESObjectWEAP* rightWeapon = rightHand ? rightHand->As<RE::TESObjectWEAP>() : nullptr;
    RE::TESObjectARMO* leftArmor = leftHand ? leftHand->As<RE::TESObjectARMO>() : nullptr;  // Para escudos

    // Valor padr�o para a m�o esquerda (0.0 = vazia)
    double leftHandType = 0.0;
    if (leftHand) {
        if (auto leftWeapon = leftHand->As<RE::TESObjectWEAP>()) {
            leftHandType = static_cast<double>(leftWeapon->GetWeaponType());
        } else if (auto leftShield = leftHand->As<RE::TESObjectARMO>()) {
            if (leftShield->IsShield()) {
                leftHandType = 11.0;  // Usando 11.0 como o tipo para escudos
            }
        }
    }

    if (!rightWeapon) {
        // Se a m�o direita est� vazia, a �nica categoria poss�vel � Unarmed
        // (a menos que voc� crie categorias para magias, etc.)
        return "Unarmed";
    }

    const auto& allCategories = AnimationManager::GetSingleton()->GetCategories();
    double rightHandType = static_cast<double>(rightWeapon->GetWeaponType());

    std::string fallbackCategory = "Unarmed";  // Um fallback caso nenhuma categoria com keyword seja encontrada

    for (const auto& pair : allCategories) {
        const WeaponCategory& category = pair.second;

        // --- L�GICA DE MATCHING ---
        // 1. Verifica se os tipos de equipamento (direita e esquerda) batem com a defini��o da categoria
        bool rightHandMatch = (category.equippedTypeValue == rightHandType);
        bool leftHandMatch =
            (category.leftHandEquippedTypeValue < 0.0 || category.leftHandEquippedTypeValue == leftHandType);

        if (rightHandMatch && leftHandMatch) {
            // 2. Se os tipos batem, verifica as keywords da m�o direita
            bool rightKeywordsMatch = category.keywords.empty();
            if (!rightKeywordsMatch) {
                for (const auto& keyword : category.keywords) {
                    if (rightWeapon->HasKeywordString(keyword)) {
                        rightKeywordsMatch = true;
                        break;
                    }
                }
            }

            // 3. Verifica as keywords da m�o esquerda (se houver alguma definida)
            bool leftKeywordsMatch = category.leftHandKeywords.empty();
            if (!leftKeywordsMatch && leftHand) {  // S� checa se a m�o esquerda tem algo
                if (auto leftWeapon = leftHand->As<RE::TESObjectWEAP>()) {
                    for (const auto& keyword : category.leftHandKeywords) {
                        if (leftWeapon->HasKeywordString(keyword)) {
                            leftKeywordsMatch = true;
                            break;
                        }
                    }
                }
                // Se for um escudo ou outro item, ele n�o ter� keywords de arma,
                // ent�o leftKeywordsMatch continuar� 'false' a menos que voc� adicione l�gica para armaduras.
            }

            // 4. Se tudo bate, encontramos nossa categoria
            if (rightKeywordsMatch && leftKeywordsMatch) {
                // Se a categoria n�o tem keywords (em nenhuma m�o), ela � um fallback
                if (category.keywords.empty() && category.leftHandKeywords.empty()) {
                    fallbackCategory = category.name;
                    continue;  // Continua procurando por uma mais espec�fica
                }
                // Se tinha keywords e elas bateram, � um match definitivo
                return category.name;
            }
        }
    }

    return fallbackCategory;
}

// NOVA VERS�O SIMPLIFICADA
std::string GetCurrentWeaponCategoryName() {
    // Esta fun��o agora simplesmente chama a fun��o principal com o jogador como alvo.
    // Isso evita duplicar c�digo e centraliza toda a l�gica em um s� lugar.
    return GetActorWeaponCategoryName(RE::PlayerCharacter::GetSingleton());
}

std::span<const SkyPromptAPI::Prompt> GlobalControl::StancesSink::GetPrompts() const {
    return prompts; }

void GlobalControl::StancesSink::ProcessEvent(SkyPromptAPI::PromptEvent event) const {
    auto eventype = event.type;
    if (!g_isWeaponDrawn) {
        return;
    }

    switch (eventype) {
        case SkyPromptAPI::kAccepted:
            if(!except) {
                except = true;
                GlobalControl::StanceChangesOpen = true;
                SkyPromptAPI::RemovePrompt(MovesetSink::GetSingleton(), g_clientID);
                SkyPromptAPI::RemovePrompt(StancesSink::GetSingleton(), g_clientID);
                if (!SkyPromptAPI::SendPrompt(StancesChangesSink::GetSingleton(), g_clientID)) {
                    logger::error("Skyprompt didnt worked Stances Changes Sink");
                }
                break;
            }
                
        case SkyPromptAPI::kUp:
            except = false;
            GlobalControl::StanceChangesOpen = false;
            SkyPromptAPI::RemovePrompt(StancesChangesSink::GetSingleton(), GlobalControl::g_clientID);
            if (SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID)){}
            if (!SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), GlobalControl::g_clientID)) {
                logger::error("Skyprompt didnt worked Moveset Sink");
            }
            break;        
        case SkyPromptAPI::kTimeout:
            if (SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID)){}
            if (!SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), GlobalControl::g_clientID)) {
                logger::error("Skyprompt didnt worked Moveset Sink");
            }
            break;        
        case SkyPromptAPI::kDeclined:
            g_currentMoveset = 0;
            g_currentStance = 0;
            UpdateSkyPromptTexts();
            //StanceText = "Stances";
            //MovesetText = "Movesets";
            SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID);
            SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
            RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("testarone", g_currentMoveset);
            RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("cycle_instance", g_currentStance);
            if (!SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), GlobalControl::g_clientID)) {
                logger::error("Skyprompt didnt worked Moveset Sink");
            }
            break;   
     
    }

}

std::span<const SkyPromptAPI::Prompt> GlobalControl::StancesChangesSink::GetPrompts() const {

    return prompts; }

void GlobalControl::StancesChangesSink::ProcessEvent(SkyPromptAPI::PromptEvent event) const {
    
    switch (event.type) {
        case SkyPromptAPI::kAccepted:
            if (event.prompt.eventID == 2) {
                g_currentStance -= 1;
                if (g_currentStance < 1) {
                    g_currentStance = 4;  // Vai para o �ltimo
                }
                UpdateSkyPromptTexts();
                SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID);
                SkyPromptAPI::SendPrompt(StancesChangesSink::GetSingleton(), g_clientID);
                SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
                SkyPromptAPI::RemovePrompt(MovesetSink::GetSingleton(), g_clientID);
                break;
            }
            if (event.prompt.eventID == 3) {
                g_currentStance += 1;
                if (g_currentStance > 4) {
                    g_currentStance = 1;  // Volta para o primeiro
                }
                UpdateSkyPromptTexts();
                SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID);
                SkyPromptAPI::SendPrompt(StancesChangesSink::GetSingleton(), g_clientID);
                SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
                SkyPromptAPI::RemovePrompt(MovesetSink::GetSingleton(), g_clientID);
                break;
            }
        case SkyPromptAPI::kTimeout:
            SkyPromptAPI::SendPrompt(StancesChangesSink::GetSingleton(), g_clientID);
            break;
        case SkyPromptAPI::kUp:
            if (event.prompt.eventID == 0) {
                GlobalControl::StanceChangesOpen = false;
                SkyPromptAPI::RemovePrompt(StancesChangesSink::GetSingleton(), GlobalControl::g_clientID);
                if (SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID)) {
                }
                if (!SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), GlobalControl::g_clientID)) {
                    logger::error("Skyprompt didnt worked Moveset Sink");
                }
            }
            break;
    }

    // REQUERIMENTO 5: Mostra a nova contagem (x/y) imediatamente
    GlobalControl::StanceChangesOpen = true;
    logger::info("O valor de MovesetText �: {}", MovesetText);
    g_currentMoveset = 1;
    RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("testarone", g_currentMoveset);
    RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("cycle_instance", g_currentStance);  
}

std::span<const SkyPromptAPI::Prompt> GlobalControl::MovesetSink::GetPrompts() const {
    return prompts; }

void GlobalControl::MovesetSink::ProcessEvent(SkyPromptAPI::PromptEvent event) const {
    auto eventype = event.type;
    if (!g_isWeaponDrawn) {
        return;
    }
    switch (eventype) {

        case SkyPromptAPI::kAccepted:
            if (!except) {
                except = true;
                GlobalControl::MovesetChangesOpen = true;
                SkyPromptAPI::RemovePrompt(StancesSink::GetSingleton(), GlobalControl::g_clientID);
                SkyPromptAPI::RemovePrompt(MovesetSink::GetSingleton(), GlobalControl::g_clientID);
                if (!SkyPromptAPI::SendPrompt(MovesetChangesSink::GetSingleton(), GlobalControl::g_clientID)) {
                    logger::error("Skyprompt didnt worked Stances Changes Sink");
                }
                SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
                break;
            }
        case SkyPromptAPI::kUp:
            except = false;
            GlobalControl::MovesetChangesOpen = false;
            SkyPromptAPI::RemovePrompt(MovesetChangesSink::GetSingleton(), GlobalControl::g_clientID);
            SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
            SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID);
            break;
        case SkyPromptAPI::kDeclined:
            g_currentMoveset = 1;
            UpdateSkyPromptTexts();
            RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("testarone", g_currentMoveset);
            //GlobalControl::MovesetText = "Moveset";
            SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
            break;
    }
}

std::span<const SkyPromptAPI::Prompt> GlobalControl::MovesetChangesSink::GetPrompts() const { 
    return prompts; }
    

void GlobalControl::MovesetChangesSink::ProcessEvent(SkyPromptAPI::PromptEvent event) const {

    // REQUERIMENTO 1, 2, 3: Pegar todas as informa��es necess�rias
    std::string category = GetCurrentWeaponCategoryName();
    // O �ndice do cache � 0-3, mas a stance no jogo � 1-4.
    int stanceIndex = GlobalControl::g_currentStance - 1;
    int maxMovesets = AnimationManager::GetMaxMovesetsFor(category, stanceIndex);

    // Se n�o h� movesets configurados para esta stance/arma, n�o faz nada.
    if (maxMovesets <= 0) {
        RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("testarone",0);  // Garante que nenhuma anima��o toque
        return;
    }

    logger::info("before kup");
    switch (event.type) {
        case SkyPromptAPI::kAccepted:
            if (event.prompt.eventID == 2) {
                g_currentMoveset -= 1;
                if (g_currentMoveset < 1) {
                    g_currentMoveset = maxMovesets;  // Vai para o �ltimo
                }
                UpdateSkyPromptTexts();
                logger::info("teste {}", MovesetText);
                RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("testarone", g_currentMoveset);
                SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
                SkyPromptAPI::SendPrompt(MovesetChangesSink::GetSingleton(), g_clientID);
                break;
            }
            if (event.prompt.eventID == 3) {
                g_currentMoveset += 1;
                if (g_currentMoveset > maxMovesets) {
                    g_currentMoveset = 1;  // Volta para o primeiro
                }
                UpdateSkyPromptTexts();
                logger::info("teste {}", MovesetText);
                RE::PlayerCharacter::GetSingleton()->SetGraphVariableInt("testarone", g_currentMoveset);
                SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
                SkyPromptAPI::SendPrompt(MovesetChangesSink::GetSingleton(), g_clientID);
                break;
            }
        case SkyPromptAPI::kTimeout:
            SkyPromptAPI::SendPrompt(MovesetChangesSink::GetSingleton(), g_clientID);
            break;
        case SkyPromptAPI::kUp:
            if (event.prompt.eventID == 1) {
                GlobalControl::MovesetChangesOpen = false;
                SkyPromptAPI::RemovePrompt(MovesetChangesSink::GetSingleton(), GlobalControl::g_clientID);
                if (SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID)) {
                }
                if (!SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), GlobalControl::g_clientID)) {
                    logger::error("Skyprompt didnt worked Moveset Sink");
                }
            }
            logger::info("kUp aceito");
            break;
    }
    
}

RE::BSEventNotifyControl GlobalControl::CameraChange::ProcessEvent(const SKSE::CameraEvent* a_event,
                                                          RE::BSTEventSource<SKSE::CameraEvent>*) {


    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }
    if (!RE::PlayerCamera::GetSingleton()->IsInThirdPerson()) {
        Cycleopen = false;
        SkyPromptAPI::RemovePrompt(StancesSink::GetSingleton(), g_clientID);
        SkyPromptAPI::RemovePrompt(MovesetSink::GetSingleton(), g_clientID);
        SkyPromptAPI::RemovePrompt(StancesChangesSink::GetSingleton(), g_clientID);
        SkyPromptAPI::RemovePrompt(MovesetChangesSink::GetSingleton(), g_clientID);
        //logger::info("me retorna aqui vei");
    }
    if (RE::PlayerCamera::GetSingleton()->IsInThirdPerson() && g_isWeaponDrawn && !Cycleopen) {
        Cycleopen = true;
        SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID);
        SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
    }
    

    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl GlobalControl::ActionEventHandler::ProcessEvent(const SKSE::ActionEvent* a_event,
                                                                         RE::BSTEventSource<SKSE::ActionEvent>*) {
  
    // REQUERIMENTO 1, 2, 3: Pegar todas as informa��es necess�rias
    std::string category = GetCurrentWeaponCategoryName();
    // O �ndice do cache � 0-3, mas a stance no jogo � 1-4.
    [[maybe_unused]] int stanceIndex = g_currentStance - 1;
    //int maxMovesets = AnimationManager::GetMaxMovesetsFor(category, stanceIndex);

    if (a_event && a_event->actor && a_event->actor->IsPlayerRef()) {
        // Jogador comeou a sacar a arma
        if (a_event->type == SKSE::ActionEvent::Type::kBeginDraw &&
            RE::PlayerCamera::GetSingleton()->IsInThirdPerson()) {
            SKSE::log::info("Arma sacada, mostrando o menu.");
            g_isWeaponDrawn = true;  // Define nosso controle como verdadeiro
            Cycleopen = true;
            // Envia os prompts para a API, fazendo o menu aparecer
            UpdateSkyPromptTexts();
            SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID);
            SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
        }
        if (a_event->type == SKSE::ActionEvent::Type::kBeginDraw) {
            SKSE::log::info("Arma sacada, mostrando o menu.");
            UpdateSkyPromptTexts();
            g_isWeaponDrawn = true;  // Define nosso controle como verdadeiro

        }
        // Jogador terminou de guardar a arma
        else if (a_event->type == SKSE::ActionEvent::Type::kEndSheathe) {
            //SKSE::log::info("Arma guardada, escondendo o menu.");
            g_isWeaponDrawn = false;  // Define nosso controle como falso
            // Limpa os prompts da API, fazendo o menu desaparecer
            SkyPromptAPI::RemovePrompt(StancesSink::GetSingleton(), g_clientID);
            SkyPromptAPI::RemovePrompt(MovesetSink::GetSingleton(), g_clientID);
            SkyPromptAPI::RemovePrompt(StancesChangesSink::GetSingleton(), g_clientID);
            SkyPromptAPI::RemovePrompt(MovesetChangesSink::GetSingleton(), g_clientID);
        }
    }
    return RE::BSEventNotifyControl::kContinue;
}



void GlobalControl::TriggerSmartRandomNumber([[maybe_unused]] const std::string& eventSource) {
    auto player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    std::string category = GetCurrentWeaponCategoryName();
    int stanceIndex = g_currentStance - 1;
    int maxMovesets = AnimationManager::GetMaxMovesetsFor(category, stanceIndex);

    if (maxMovesets <= 0) {  // Alterado para <= 0, pois 1 moveset n�o tem o que ciclar.
        return;
    }

    int nextMoveset = 1;

    // --- IN�CIO DA NOVA L�GICA ---
    if (Settings::RandomCycle) {  // Se a nova checkbox "Random cycle" estiver ativa
        if (maxMovesets > 1) {
            // Nova l�gica aleat�ria sem restri��es
            std::random_device rd;
            std::mt19937 gen(rd());
            // Gera um n�mero entre 1 e maxMovesets, garantindo que n�o seja o mesmo que o atual.
            std::uniform_int_distribution<> distrib(1, maxMovesets);
            do {
                nextMoveset = distrib(gen);
            } while (nextMoveset == g_currentMoveset);
        }
    } else {  // Se for o cycle moveset padr�o (agora sequencial)
        nextMoveset = g_currentMoveset + 1;
        if (nextMoveset > maxMovesets) {
            nextMoveset = 1;  // Volta para o primeiro
        }
    }
    // --- FIM DA NOVA L�GICA ---

    g_currentMoveset = nextMoveset;
    player->SetGraphVariableInt("testarone", g_currentMoveset);
    UpdateSkyPromptTexts();

    // A l�gica de comboState n�o � mais necess�ria para o modo sequencial ou o novo modo aleat�rio
    // g_comboState.previousMoveset = g_comboState.lastMoveset;
    // g_comboState.lastMoveset = nextMoveset;

    if (g_isWeaponDrawn && RE::PlayerCamera::GetSingleton()->IsInThirdPerson()) {
        SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
    }
}

bool GlobalControl::IsAnyMenuOpen() { 
    const auto ui = RE::UI::GetSingleton();
    for (const auto a_name : blockedMenus) {
        if (ui->IsMenuOpen(a_name)) {
            return true;
        }
    }
    return false;
}


bool GlobalControl::IsThirdPerson() {
    return !RE::PlayerCamera::GetSingleton()->IsInFirstPerson();
}

RE::BSEventNotifyControl GlobalControl::MenuOpen::ProcessEvent(const RE::MenuOpenCloseEvent* event,
                                                               RE::BSTEventSource<RE::MenuOpenCloseEvent>*) {
    if (!event) {
             return RE::BSEventNotifyControl::kContinue;
    }

    // REQUERIMENTO 1, 2, 3: Pegar todas as informa��es necess�rias
    std::string category = GetCurrentWeaponCategoryName();
    // O �ndice do cache � 0-3, mas a stance no jogo � 1-4.
    int stanceIndex = GlobalControl::g_currentStance - 1;
    [[maybe_unused]] int maxMovesets = AnimationManager::GetMaxMovesetsFor(category, stanceIndex);
    if (!IsAnyMenuOpen() && IsThirdPerson() && g_isWeaponDrawn && !Cycleopen) {
        Cycleopen = true;
        UpdateSkyPromptTexts();
        //logger::info("O valor de MovesetText �: {}", MovesetText);
        SkyPromptAPI::SendPrompt(StancesSink::GetSingleton(), g_clientID);
        SkyPromptAPI::SendPrompt(MovesetSink::GetSingleton(), g_clientID);
    } 
    if (IsAnyMenuOpen() && IsThirdPerson()) {
        Cycleopen = false;
        UpdateSkyPromptTexts();
        SkyPromptAPI::RemovePrompt(StancesSink::GetSingleton(), g_clientID);
        SkyPromptAPI::RemovePrompt(MovesetSink::GetSingleton(), g_clientID);
    }
    
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl GlobalControl::AnimationEventHandler::ProcessEvent(
    const RE::BSAnimationGraphEvent* a_event, RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {

     // --- LOG DE DIAGN�STICO ---
    // Apenas loga se o timer deveria estar rodando, para n�o poluir o log 100% do tempo
    if (g_comboState.isTimerRunning) {
        auto now = std::chrono::steady_clock::now();
        auto time_left_ms = std::chrono::duration_cast<std::chrono::milliseconds>(g_comboState.comboTimeoutTimestamp - now).count();
        //SKSE::log::info("[UpdateHandler] Checando timer... g_comboState.isTimerRunning: {}. Tempo restante: {} ms", g_comboState.isTimerRunning,time_left_ms);
    }
    

    if (a_event && a_event->holder && a_event->holder->IsPlayerRef()) {
        const std::string_view eventName = a_event->tag;
        if (g_comboState.isTimerRunning && std::chrono::steady_clock::now() >= g_comboState.comboTimeoutTimestamp) {
            g_comboState.isTimerRunning = false;

            // --- LOG DE DIAGN�STICO ---
            //SKSE::log::info("[UpdateHandler] TIMEOUT! Fim de combo.");

            if (Settings::CycleMoveset) {
                SKSE::GetTaskInterface()->AddTask([]() { TriggerSmartRandomNumber("Fim de Combo (C++)"); });
            }
        }
        else if(eventName == "weaponSwing" || eventName == "weaponLeftSwing" ||
            eventName == "h2hAttack" || eventName == "PowerAttack_Start_end") {
            //SKSE::log::info("[AnimationEventHandler] Evento '{}' detectado. Timer INICIADO. g_comboState.isTimerRunning AGORA �: {}",eventName, g_comboState.isTimerRunning);
            //SKSE::log::info("[AnimationEventHandler] Evento '{}' detectado. Timer INICIADO.", eventName);
            // Apenas definimos o estado e o momento em que o combo deve terminar.
            g_comboState.isTimerRunning = true;
            auto timeout_ms = std::chrono::milliseconds(static_cast<int>(Settings::CycleTimer * 1000));
            g_comboState.comboTimeoutTimestamp = std::chrono::steady_clock::now() + timeout_ms;
            

        } else if (eventName == "weaponDraw" || eventName == "weaponSheathe") {
            g_comboState.isTimerRunning = false;  // Cancela qualquer combo pendente
            if (Settings::CycleMoveset) {
                TriggerSmartRandomNumber(std::string(eventName));
            }
        }
    }
    return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl GlobalControl::NpcCycleSink::ProcessEvent(const RE::BSAnimationGraphEvent* a_event,
                                                                   RE::BSTEventSource<RE::BSAnimationGraphEvent>*) {

    // a_event->holder nos d� o ator que gerou o evento.
    if (a_event && a_event->holder) {
        auto actor = a_event->holder->As<RE::Actor>();
        if (!actor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::FormID formID = actor->GetFormID();
        const std::string_view eventName = a_event->tag;

        if (eventName == "weaponSwing") {
            // --- L�gica de atualiza��o com Mutex ---
            std::lock_guard<std::mutex> lock(g_comboStateMutex);  // Trava o mutex (destrava automaticamente no fim do escopo)

            // Acessa (ou cria, se n�o existir) o estado de combo para ESTE ator espec�fico
            auto& state = g_npcComboStates[formID];

            state.isTimerRunning = true;
            auto timeout_ms = std::chrono::milliseconds(static_cast<int>(fComboTimeout * 1000));
            state.comboTimeoutTimestamp = std::chrono::steady_clock::now() + timeout_ms;

            //SKSE::log::info("[AnimationEventHandler] Ator {:08X} iniciou/resetou combo com evento '{}'.", formID, eventName);

        } else if (eventName == "weaponDraw" || eventName == "weaponSheathe") {
            std::lock_guard<std::mutex> lock(g_comboStateMutex);
            // Se o ator estiver no nosso mapa, cancela o timer dele.
            if (g_npcComboStates.count(formID)) {
                g_npcComboStates[formID].isTimerRunning = false;
            }
        }
    }
    // Lista para guardar os FormIDs dos atores cujo combo expirou.
    // N�o podemos modificar o mapa enquanto iteramos sobre ele, ent�o guardamos para depois.
    std::vector<RE::FormID> expiredCombos;
    auto now = std::chrono::steady_clock::now();

    {  // Criamos um escopo para o lock
        std::lock_guard<std::mutex> lock(g_comboStateMutex);

        for (auto& [formID, state] : g_npcComboStates) {
            if (state.isTimerRunning && now >= state.comboTimeoutTimestamp) {
                state.isTimerRunning = false;
                expiredCombos.push_back(formID);
            }
        }
    }  // O mutex � liberado aqui

    // Agora, fora do lock, disparamos o evento para cada combo que expirou.
    for (const auto& formID : expiredCombos) {
        // Precisamos encontrar o ponteiro do ator a partir do FormID
        auto actor = RE::TESForm::LookupByID<RE::Actor>(formID);
        if (actor) {
            //SKSE::log::info("[UpdateHandler] Combo do ator {:08X} expirou.", formID);
            // Adicionamos a l�gica para chamar a fun��o para o ator espec�fico
            // Usando SKSE::GetTaskInterface() ainda � uma boa pr�tica
            SKSE::GetTaskInterface()->AddTask([actor]() { NPCrandomNumber(actor, "Fim de Combo"); });
        }
    }
    return RE::BSEventNotifyControl::kContinue;
}

void GlobalControl::NPCrandomNumber(RE::Actor* targetActor, const std::string& eventSource) {
    if (!targetActor) return;

    std::string category = GetActorWeaponCategoryName(targetActor);

    // Pega a lista de �ndices de movesets DISPON�VEIS AGORA
    std::vector<int> availableMovesets =
        AnimationManager::GetSingleton()->GetAvailableMovesetIndices(targetActor, category);

    if (availableMovesets.size() < 2) {  // N�o h� o que ciclar se tiver 0 ou 1 op��o
        // Opcional: Se houver 1, voc� pode setar para ele. Se 0, n�o faz nada.
        if (!availableMovesets.empty()) {
            targetActor->SetGraphVariableInt("testarone", availableMovesets[0]);
        }
        return;
    }

    // A l�gica de "random inteligente" agora opera sobre a lista de movesets v�lidos
    RE::FormID formID = targetActor->GetFormID();
    std::lock_guard<std::mutex> lock(g_comboStateMutex);
    auto& state = g_npcComboStates[formID];

    // Filtra a lista para n�o repetir os 2 �ltimos
    std::vector<int> choices = availableMovesets;
    choices.erase(std::remove(choices.begin(), choices.end(), state.lastMoveset), choices.end());
    choices.erase(std::remove(choices.begin(), choices.end(), state.previousMoveset), choices.end());

    if (choices.empty()) {  // Se todos os v�lidos foram usados recentemente, usa a lista completa
        choices = availableMovesets;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, static_cast<int>(choices.size() - 1));
    int chosenPlaylistIndex = choices[distrib(gen)];

    // Atualiza o estado e a vari�vel do jogo
    targetActor->SetGraphVariableInt("testarone", chosenPlaylistIndex);
    state.previousMoveset = state.lastMoveset;
    state.lastMoveset = chosenPlaylistIndex;

    SKSE::log::info("{} (Ator {:08X}): Escolheu o moveset #{}", eventSource, formID, chosenPlaylistIndex);
}

RE::BSEventNotifyControl GlobalControl::NpcCombatTracker::ProcessEvent(const RE::TESCombatEvent* a_event,
                                                                       RE::BSTEventSource<RE::TESCombatEvent>*) {
    auto actor = a_event->actor.get();
    auto targetActor = a_event->targetActor.get();
    auto newState = a_event->newState;
    auto* npc = actor->As<RE::Actor>();


    if (!a_event || !a_event->actor) {
        return RE::BSEventNotifyControl::kContinue;
    }
    if (actor && actor->IsPlayerRef()) {
        // Ignorar eventos do jogador
        return RE::BSEventNotifyControl::kContinue;
    }

    if (actor) {
        switch (a_event->newState.get()) {
            case RE::ACTOR_COMBAT_STATE::kCombat:
                SKSE::log::info("{} entrou em combate com {}", actor->GetName(),
                                targetActor ? targetActor->GetName() : "alvo desconhecido");
                GlobalControl::NpcCombatTracker::RegisterSink(npc);

                break;
            case RE::ACTOR_COMBAT_STATE::kSearching:
                // O NPC est� procurando um alvo
                // Coloque seu c�digo aqui
                break;
            case RE::ACTOR_COMBAT_STATE::kNone:
                SKSE::log::info("{} saiu de combate com {}", actor->GetName(),
                                targetActor ? targetActor->GetName() : "alvo desconhecido");
                GlobalControl::NpcCombatTracker::UnregisterSink(npc);
                break;
        }
    }


    return RE::BSEventNotifyControl::kContinue;
}

void GlobalControl::NpcCombatTracker::RegisterSink(RE::Actor* a_actor) {
    if (!a_actor || a_actor->IsPlayerRef()) return;

    std::unique_lock lock(g_mutex);
    if (g_trackedNPCs.find(a_actor->GetFormID()) == g_trackedNPCs.end()) {
        a_actor->AddAnimationGraphEventSink(&g_npcSink);
        g_trackedNPCs.insert(a_actor->GetFormID());
        //SKSE::log::info("[NpcCombatTracker] Come�ando a rastrear anima��es do ator {:08X}", a_actor->GetFormID());
    }
}

void GlobalControl::NpcCombatTracker::UnregisterSink(RE::Actor* a_actor) {
    if (!a_actor || a_actor->IsPlayerRef()) return;

    std::unique_lock lock(g_mutex);
    if (g_trackedNPCs.find(a_actor->GetFormID()) != g_trackedNPCs.end()) {
        a_actor->RemoveAnimationGraphEventSink(&g_npcSink);
        g_trackedNPCs.erase(a_actor->GetFormID());
        //SKSE::log::info("[NpcCombatTracker] Parando de rastrear anima��es do ator {:08X}", a_actor->GetFormID());
    }
}

void GlobalControl::NpcCombatTracker::RegisterSinksForExistingCombatants() {
    SKSE::log::info("[NpcCombatTracker] Verificando NPCs j� em combate ap�s carregar o jogo...");

    auto* processLists = RE::ProcessLists::GetSingleton();
    if (!processLists) {
        SKSE::log::warn("[NpcCombatTracker] N�o foi poss�vel obter ProcessLists.");
        return;
    }

    // Itera sobre todos os atores que est�o "ativos" no jogo
    for (auto& actorHandle : processLists->highActorHandles) {
        if (auto actor = actorHandle.get().get()) {
            // A fun��o IsInCombat() nos diz se o ator j� est� em um estado de combate
            if (!actor->IsPlayerRef() && actor->IsInCombat()) {
                SKSE::log::info("[NpcCombatTracker] Ator '{}' ({:08X}) j� est� em combate. Registrando sink...",
                                actor->GetName(), actor->GetFormID());
                // Usamos a mesma fun��o de registro que j� existe!
                RegisterSink(actor);
            }
        }
    }
    SKSE::log::info("[NpcCombatTracker] Verifica��o conclu�da.");
}

void GlobalControl::UpdateSkyPromptTexts() {
    auto animManager = AnimationManager::GetSingleton();
    std::string category = GetCurrentWeaponCategoryName();

    // --- L�GICA PARA STANCES  ---
    if (g_currentStance == 0) {
        // Caso especial: Nenhuma stance ativa.
        StanceText = "Stances";  // Define um texto padr�o.
        // 'Next' aponta para a primeira stance (�ndice 0).
        StanceNextText = animManager->GetStanceName(category, 0);
        // 'Back' aponta para a �ltima stance (�ndice 3).
        StanceBackText = animManager->GetStanceName(category, 3);
    } else {
        // L�gica original para quando uma stance est� ativa (1 a 4).
        int currentStanceIndex = g_currentStance - 1;  // Converte para �ndice 0-3
        int nextStanceIndex = (currentStanceIndex + 1) % 4;
        int backStanceIndex = (currentStanceIndex - 1 + 4) % 4;
        StanceText = animManager->GetStanceName(category, currentStanceIndex);
        StanceNextText = animManager->GetStanceName(category, nextStanceIndex);
        StanceBackText = animManager->GetStanceName(category, backStanceIndex);
    }
    int validStanceIndexForMoveset = g_currentStance - 1;

    // --- L�GICA PARA MOVESETS  ---
    int maxMovesets = animManager->GetMaxMovesetsFor(category, validStanceIndexForMoveset);
    int currentMovesetIndex = g_currentMoveset;  // 1-N
    if (maxMovesets > 0) {
        int dirState = InputListener::GetDirectionalState();
        SKSE::log::info("[UpdateSkyPromptTexts] Chamando GetCurrentMovesetName com dirState: {}", dirState);
        std::string currentMovesetName =
            animManager->GetCurrentMovesetName(category, validStanceIndexForMoveset, currentMovesetIndex, dirState);
        MovesetText = std::format("{} ({}/{})", currentMovesetName, currentMovesetIndex, maxMovesets);

        if (maxMovesets > 1) {
            int nextMovesetIndex = (currentMovesetIndex % maxMovesets) + 1;
            int backMovesetIndex = (currentMovesetIndex - 2 + maxMovesets) % maxMovesets + 1;
            MovesetNextText =
                animManager->GetCurrentMovesetName(category, validStanceIndexForMoveset, nextMovesetIndex, 0);
            MovesetBackText =
                animManager->GetCurrentMovesetName(category, validStanceIndexForMoveset, backMovesetIndex, 0);
        } else {
            MovesetNextText = "Back";
            MovesetBackText = "Next";
        }
    } else {
        MovesetText = "Movesets";
        MovesetNextText = "Back";
        MovesetBackText = "Next";
    }

    stance_actual = SkyPromptAPI::Prompt(StanceText, 0, 0, SkyPromptAPI::PromptType::kSinglePress, Settings::ShowMenu ? 20 : 0,
                                         Stances_menu,
                                         0xFFFFFFFF, 0.999f);
    moveset_actual = SkyPromptAPI::Prompt(MovesetText, 1, 0, SkyPromptAPI::PromptType::kSinglePress,
                                          Settings::ShowMenu ? 20 : 0, Moveset_menu,
                                          0xFFFFFFFF, 0.999f);
    menu_stance = SkyPromptAPI::Prompt(StanceText, 0, 0, SkyPromptAPI::PromptType::kHoldAndKeep,
                                       Settings::ShowMenu ? 20 : 0, Stances_menu);
    stance_next = SkyPromptAPI::Prompt(StanceNextText, 3, 0, SkyPromptAPI::PromptType::kSinglePress,
                                       Settings::ShowMenu ? 20 : 0, Next_key);
    stance_back = SkyPromptAPI::Prompt(StanceBackText, 2, 0, SkyPromptAPI::PromptType::kSinglePress,
                                       Settings::ShowMenu ? 20 : 0, Back_key);
    menu_moveset = SkyPromptAPI::Prompt(MovesetText, 1, 0, SkyPromptAPI::PromptType::kHoldAndKeep,
                                        Settings::ShowMenu ? 20 : 0, Moveset_menu);
    moveset_next = SkyPromptAPI::Prompt(MovesetNextText, 3, 0, SkyPromptAPI::PromptType::kSinglePress,
                                        Settings::ShowMenu ? 20 : 0, Next_key);
    moveset_back = SkyPromptAPI::Prompt(MovesetBackText, 2, 0, SkyPromptAPI::PromptType::kSinglePress,
                                        Settings::ShowMenu ? 20 : 0, Back_key);

    StancesSink::GetSingleton()->UpdatePrompts();
    StancesChangesSink::GetSingleton()->UpdatePrompts();
    MovesetSink::GetSingleton()->UpdatePrompts();
    MovesetChangesSink::GetSingleton()->UpdatePrompts();
}