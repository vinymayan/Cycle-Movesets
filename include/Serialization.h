

class InputListener : public RE::BSTEventSink<RE::InputEvent*> {
public:
    // Singleton para garantir que exista apenas uma instância
    static InputListener* GetSingleton() {
        static InputListener singleton;
        return &singleton;
    }
    static inline RE::INPUT_DEVICE lastUsedDevice = RE::INPUT_DEVICE::kKeyboard;
    // A função que processa os eventos de input do jogo
    virtual RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                                  RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;
    static int GetDirectionalState() { return directionalState; };
    

protected:

private:
    // Função para calcular a direção com base nas teclas pressionadas
    void UpdateDirectionalState();
    static inline int directionalState = 0;
    // Variáveis para rastrear o estado de cada tecla de movimento
    bool w_pressed = false;
    bool a_pressed = false;
    bool s_pressed = false;
    bool d_pressed = false;

    // Controle
    bool c_up = false;
    bool c_left = false;
    bool c_down = false;
    bool c_right = false;
};