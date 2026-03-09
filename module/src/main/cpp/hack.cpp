//
// Created by Perfare on 2020/7/4.
//
#include "hack.h"
#include "il2cpp_dump.h"
#include "log.h"
#include "xdl.h"
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <sys/system_properties.h>
#include <dlfcn.h>
#include <jni.h>
#include <thread>
#include <sys/mman.h>
#include <linux/unistd.h>
#include <array>

// --- Ajout pour Photon kick TCO ---
#include <android/log.h>  // Déjà inclus via log.h, mais au cas où

#define LOG_TAG "TCO_KICK"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

// RVA exacts de ton dump.cs (Photon.Pun.PhotonNetwork)
const uintptr_t RVA_SET_MASTER_CLIENT = 0x1947664;   // SetMasterClient(PhotonPlayer masterClientPlayer)
const uintptr_t RVA_CLOSE_CONNECTION   = 0x194765C;   // CloseConnection(PhotonPlayer kickPlayer)

// Fonction pour appeler SetMasterClient (devient master)
bool SetMasterClient(void* localPlayer) {
    void* libil2cpp = xdl_open("libil2cpp.so", 0);
    if (!libil2cpp) {
        LOGD("Erreur: libil2cpp.so non trouvé pour hook");
        return false;
    }

    // Récup base address de libil2cpp.so
    uintptr_t base = (uintptr_t)libil2cpp;

    // RVA + base = adresse réelle de la méthode
    void* methodAddr = (void*)(base + RVA_SET_MASTER_CLIENT);

    // Appel direct (assume signature bool (*)(PhotonPlayer*))
    typedef bool (*SetMasterClient_t)(void* player);
    SetMasterClient_t SetMasterClientFunc = (SetMasterClient_t)methodAddr;

    bool success = SetMasterClientFunc(localPlayer);
    LOGD("SetMasterClient appelé : %s", success ? "OK" : "Échec");

    xdl_close(libil2cpp);
    return success;
}

// Fonction pour kicker (CloseConnection)
bool CloseConnection(void* targetPlayer) {
    void* libil2cpp = xdl_open("libil2cpp.so", 0);
    if (!libil2cpp) return false;

    uintptr_t base = (uintptr_t)libil2cpp;
    void* methodAddr = (void*)(base + RVA_CLOSE_CONNECTION);

    typedef bool (*CloseConnection_t)(void* player);
    CloseConnection_t CloseConnectionFunc = (CloseConnection_t)methodAddr;

    bool success = CloseConnectionFunc(targetPlayer);
    LOGD("CloseConnection appelé sur target : %s", success ? "OK" : "Échec");

    xdl_close(libil2cpp);
    return success;
}

// Fonction principale pour tester le kick (appelle après dump)
void TryPhotonKick() {
    // Pour tester : on assume que tu es dans une room PvP
    // Il faut récupérer LocalPlayer et un targetPlayer (ActorNr 2 ex)
    // Pour un test basique, on log seulement les RVA
    LOGD("Photon Kick test lancé ! RVA SetMaster: 0x%lx, Close: 0x%lx",
         RVA_SET_MASTER_CLIENT, RVA_CLOSE_CONNECTION);

    // TODO : Récup LocalPlayer (via il2cpp_class_get_method_from_name + invoke "get_LocalPlayer")
    // void* localPlayer = ... (utilise il2cpp-api-functions.h si disponible)
    // SetMasterClient(localPlayer);

    // void* target = ... (scan ou hook room players)
    // CloseConnection(target);
}

// Fonction hack_start originale + notre ajout
void hack_start(const char *game_data_dir) {
    bool load = false;
    for (int i = 0; i < 10; i++) {
        void *handle = xdl_open("libil2cpp.so", 0);
        if (handle) {
            load = true;
            il2cpp_api_init(handle);
            il2cpp_dump(game_data_dir);

            // --- AJOUT ICI : après dump, on peut hooker Photon ---
            LOGD("IL2CPP dump terminé, lancement Photon kick test...");
            TryPhotonKick();

            break;
        } else {
            sleep(1);
        }
    }
    if (!load) {
        LOGI("libil2cpp.so not found in thread %d", gettid());
    }
}

// Le reste du fichier reste IDENTIQUE (GetLibDir, NativeBridgeLoad, hack_prepare, JNI_OnLoad)
std::string GetLibDir(JavaVM *vms) {
    // ... code original inchangé ...
}

static std::string GetNativeBridgeLibrary() {
    // ... code original inchangé ...
}

struct NativeBridgeCallbacks {
    // ... code original inchangé ...
};

bool NativeBridgeLoad(const char *game_data_dir, int api_level, void *data, size_t length) {
    // ... code original inchangé ...
}

void hack_prepare(const char *game_data_dir, void *data, size_t length) {
    // ... code original inchangé ...
}

#if defined(__arm__) || defined(__aarch64__)
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    auto game_data_dir = (const char *) reserved;
    std::thread hack_thread(hack_start, game_data_dir);
    hack_thread.detach();
    return JNI_VERSION_1_6;
}
#endif
