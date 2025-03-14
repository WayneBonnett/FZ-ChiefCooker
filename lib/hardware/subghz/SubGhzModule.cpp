#ifndef _SUBGHZ_MODULE_CLASS_
#define _SUBGHZ_MODULE_CLASS_

#include "lib/hardware/subghz/SubGhzPayload.cpp"
#include <functional>

#include <furi.h>
#include <furi_hal.h>

#include <applications/drivers/subghz/cc1101_ext/cc1101_ext_interconnect.h>
#include <lib/subghz/devices/cc1101_int/cc1101_int_interconnect.h>

#include <lib/subghz/receiver.h>
#include <lib/subghz/transmitter.h>
#include <lib/subghz/devices/devices.h>
#include <lib/subghz/devices/cc1101_configs.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/subghz_worker.h>

#include "lib/HandlerContext.cpp"

#include "SubGhzState.cpp"
#include "data/SubGhzReceivedDataImpl.cpp"

#include "lib/hardware/notification/Notification.cpp"

using namespace std;

#undef LOG_TAG
#define LOG_TAG "SUB_GHZ"

class SubGhzModule {
private:
    SubGhzEnvironment* environment;
    const SubGhzDevice* device;
    SubGhzReceiver* receiver;
    SubGhzWorker* worker;
    SubGhzTransmitter* transmitter;
    IDestructable* receiveHandler = NULL;
    FuriTimer* txCompleteCheckTimer;
    function<void()> txCompleteHandler;
    int repeatsLeft = 0;
    SubGhzPayload* currentPayload;

    bool isExternal;
    SubGhzState state = IDLE;
    bool receiveAfterTransmission = false;

    static void captureCallback(SubGhzReceiver* receiver, SubGhzProtocolDecoderBase* decoderBase, void* context) {
        UNUSED(receiver);

        if(context == NULL) {
            FURI_LOG_W(LOG_TAG, "SubGhz module has NULL receive handler!");
            return;
        }

        auto handlerContext = (HandlerContext<function<void(SubGhzReceivedData*)>>*)context;
        handlerContext->GetHandler()(new SubGhzReceivedDataImpl(decoderBase));
    }

    static void txCompleteCheckCallback(void* context) {
        SubGhzModule* subghz = (SubGhzModule*)context;
        if(subghz_devices_is_async_complete_tx(subghz->device)) {
            if(subghz->repeatsLeft-- > 0 && subghz->currentPayload != NULL) {
                subghz->startTransmission();
                return;
            }

            furi_timer_stop(subghz->txCompleteCheckTimer);
            FURI_LOG_I(LOG_TAG, "TX complete!");
            if(subghz->txCompleteHandler != NULL) {
                subghz->txCompleteHandler();
            } else {
                subghz->DefaultAfterTransmissionHandler();
            }
        }
    }

public:
    SubGhzModule(uint32_t frequency) {
        environment = subghz_environment_alloc();
        subghz_environment_set_protocol_registry(environment, &subghz_protocol_registry);

        subghz_devices_init();
        furi_hal_power_enable_otg();
        device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_EXT_NAME);
        if(!subghz_devices_is_connect(device)) {
            furi_hal_power_disable_otg();
            device = subghz_devices_get_by_name(SUBGHZ_DEVICE_CC1101_INT_NAME);
            isExternal = false;
        } else {
            isExternal = true;
        }

        subghz_devices_begin(device);
        subghz_devices_load_preset(device, FuriHalSubGhzPresetOok650Async, NULL);

        SetFrequency(frequency);

        receiver = subghz_receiver_alloc_init(environment);
        subghz_receiver_set_filter(receiver, SubGhzProtocolFlag_Decodable);

        worker = subghz_worker_alloc();
        subghz_worker_set_overrun_callback(worker, (SubGhzWorkerOverrunCallback)subghz_receiver_reset);
        subghz_worker_set_pair_callback(worker, (SubGhzWorkerPairCallback)subghz_receiver_decode);
        subghz_worker_set_context(worker, receiver);

        txCompleteCheckTimer = furi_timer_alloc(txCompleteCheckCallback, FuriTimerTypePeriodic, this);
    }

    uint32_t SetFrequency(uint32_t frequency) {
        return subghz_devices_set_frequency(device, frequency);
    }

    void SetReceiveAfterTransmission(bool value) {
        this->receiveAfterTransmission = value;
    }

    void DefaultAfterTransmissionHandler() {
        if(receiveAfterTransmission) {
            ReceiveAsync();
        } else {
            PutToIdle();
        }
    }

    void SetReceiveHandler(function<void(SubGhzReceivedData*)> handler) {
        subghz_receiver_set_rx_callback(receiver, captureCallback, receiveHandler = new HandlerContext(handler));
    }

    void ReceiveAsync() {
        PutToIdle();

        subghz_devices_flush_rx(device);
        subghz_devices_start_async_rx(device, (void*)subghz_worker_rx_callback, worker);
        subghz_worker_start(worker);

        state = RECEIVING;
    }

    void SetTransmitCompleteHandler(function<void()> txCompleteHandler) {
        this->txCompleteHandler = txCompleteHandler;
    }

    void Transmit(SubGhzPayload* payload) {
        if(state != TRANSMITTING) {
            PutToIdle();
            state = TRANSMITTING;
        } else {
            furi_timer_stop(txCompleteCheckTimer);
            delete currentPayload;
        }

        Notification::Play(&sequence_blink_start_magenta);

        currentPayload = payload;
        repeatsLeft = payload->GetRequiredSofwareRepeats() - 1;

        startTransmission();

        uint32_t interval = furi_kernel_get_tick_frequency() / 100; // every 10 ms
        furi_timer_start(txCompleteCheckTimer, interval);
    }

private:
    void startTransmission() {
        stopTransmission();

        transmitter = subghz_transmitter_alloc_init(environment, currentPayload->GetProtocol());
        subghz_transmitter_deserialize(transmitter, currentPayload->GetFlipperFormat());
        subghz_devices_flush_tx(device);
        subghz_devices_start_async_tx(device, (void*)subghz_transmitter_yield, transmitter);
    }

    void stopTransmission() {
        if(transmitter != NULL) {
            subghz_devices_stop_async_tx(device);
            subghz_transmitter_free(transmitter);
            transmitter = NULL;
        }
    }

public:
    void StopReceive() {
        subghz_worker_stop(worker);
        subghz_devices_stop_async_rx(device);
        subghz_devices_idle(device);
        state = IDLE;
    }

    void StopTranmit() {
        Notification::Play(&sequence_blink_stop);

        repeatsLeft = 0;
        delete currentPayload;

        furi_timer_stop(txCompleteCheckTimer);
        stopTransmission();
        subghz_devices_idle(device);

        state = IDLE;
    }

    void PutToIdle() {
        switch(state) {
        case RECEIVING:
            StopReceive();
            break;

        case TRANSMITTING:
            StopTranmit();
            break;

        default:
        case IDLE:
            break;
        }
    }

    bool IsExternal() {
        return isExternal;
    }

    ~SubGhzModule() {
        PutToIdle();

        if(txCompleteCheckTimer != NULL) {
            furi_timer_free(txCompleteCheckTimer);
        }

        if(furi_hal_power_is_otg_enabled()) {
            furi_hal_power_disable_otg();
        }

        if(receiveHandler != NULL) {
            delete receiveHandler;
        }

        if(worker != NULL) {
            subghz_worker_free(worker);
            worker = NULL;
        }

        if(receiver != NULL) {
            subghz_receiver_free(receiver);
            receiver = NULL;
        }

        if(environment != NULL) {
            subghz_environment_free(environment);
            environment = NULL;
        }

        if(device != NULL) {
            subghz_devices_end(device);
            subghz_devices_deinit();
            device = NULL;
        }
    }
};

#endif //_SUBGHZ_MODULE_CLASS_
