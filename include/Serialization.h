

class InputListener : public RE::BSTEventSink<RE::InputEvent*> {
public:
    // Singleton para garantir que exista apenas uma inst�ncia
    static InputListener* GetSingleton() {
        static InputListener singleton;
        return &singleton;
    }
    static inline RE::INPUT_DEVICE lastUsedDevice = RE::INPUT_DEVICE::kKeyboard;
    // A fun��o que processa os eventos de input do jogo
    virtual RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_event,
                                                  RE::BSTEventSource<RE::InputEvent*>* a_eventSource) override;
    static int GetDirectionalState() { return directionalState; };
    

protected:

private:
    // Fun��o para calcular a dire��o com base nas teclas pressionadas
    void UpdateDirectionalState();
    static inline int directionalState = 0;
    // Vari�veis para rastrear o estado de cada tecla de movimento
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