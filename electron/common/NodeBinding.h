#ifndef atom_NodeBinding_h
#define atom_NodeBinding_h

#include "v8.h"

namespace node {
class Environment;
}

namespace gin {
class Dictionary;
}

typedef struct uv_loop_s uv_loop_t;

namespace atom {

class NodeBindings {
public:
    NodeBindings(bool isBrowser, uv_loop_t* uvLoop);
    ~NodeBindings();

    void bindFunction(gin::Dictionary* dict);

    node::Environment* createEnvironment(v8::Local<v8::Context> context);
    void loadEnvironment();
private:
    bool m_isBrowser;
    uv_loop_t* m_uvLoop;
    node::Environment* m_env;
};

} // atom

#endif // atom_NodeBinding_h