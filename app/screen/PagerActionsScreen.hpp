#pragma once

#include "lib/String.hpp"
#include "app/AppConfig.hpp"
#include "app/pager/PagerDataStored.hpp"
#include "app/pager/decoder/PagerDecoder.hpp"
#include "app/pager/protocol/PagerProtocol.hpp"
#include "lib/hardware/subghz/SubGhzModule.hpp"
#include "lib/ui/view/UiView.hpp"
#include "lib/ui/view/SubMenuUiView.hpp"
#include "app/screen/BatchTransmissionScreen.hpp"
#include "lib/FlipperDolphin.hpp"
#include "lib/ui/UiManager.hpp"

class PagerActionsScreen {
private:
    AppConfig* config;
    SubMenuUiView* submenu;
    PagerDecoder* decoder;
    PagerProtocol* protocol;
    SubGhzModule* subghz;
    PagerDataGetter getPager;
    BatchTransmissionScreen* batchTransmissionScreen;

    String headerStr;
    String resendToAllStr;
    String resendToCurrentStr;
    String** actionsStrings;

    uint32_t currentPager = 0;
    bool transmittingBatch = false;

public:
    PagerActionsScreen(
        AppConfig* config,
        PagerDataGetter pagerGetter,
        PagerDecoder* decoder,
        PagerProtocol* protocol,
        SubGhzModule* subghz
    ) {
        this->config = config;
        this->getPager = pagerGetter;
        this->decoder = decoder;
        this->protocol = protocol;
        this->subghz = subghz;

        PagerDataStored* pager = getPager();
        PagerAction currentAction = decoder->GetAction(pager->data);
        uint8_t actionValue = decoder->GetActionValue(pager->data);
        uint16_t stationNum = decoder->GetStation(pager->data);
        uint16_t pagerNum = decoder->GetPager(pager->data);

        submenu = new SubMenuUiView(headerStr.format("Station %d actions", stationNum));
        submenu->SetOnDestroyHandler(HANDLER(&PagerActionsScreen::destroy));
        submenu->SetOnReturnToViewHandler(HANDLER(&PagerActionsScreen::onReturn));

        submenu->AddItem(
            resendToAllStr.format("Resend %d (%s) to ALL", actionValue, PagerActions::GetDescription(currentAction)),
            HANDLER_1ARG(&PagerActionsScreen::resendToAll)
        );

        if(currentAction == UNKNOWN) {
            submenu->AddItem(
                resendToCurrentStr.format("Resend only to pager %d", pagerNum), HANDLER_1ARG(&PagerActionsScreen::resendSingle)
            );
        }

        actionsStrings = new String*[decoder->GetSupportedActions().size()];
        for(size_t i = 0; i < decoder->GetSupportedActions().size(); i++) {
            PagerAction action = decoder->GetSupportedActions()[i];
            if(PagerActions::IsPagerActionSpecial(action)) {
                actionsStrings[i] = new String("Trigger action %s", PagerActions::GetDescription(action));
            } else {
                actionsStrings[i] = new String("%s only pager %d", PagerActions::GetDescription(action), pagerNum);
            }

            submenu->AddItem(actionsStrings[i]->cstr(), [this, action](uint32_t) { sendAction(action); });
        }

        subghz->SetTransmitCompleteHandler(HANDLER(&PagerActionsScreen::txComplete));
    }

private:
    void resendToAll(uint32_t) {
        currentPager = 0;
        transmittingBatch = true;

        batchTransmissionScreen = new BatchTransmissionScreen(config->MaxPagerForBatchOrDetection);
        UiManager::GetInstance()->PushView(batchTransmissionScreen->GetView());
        sendCurrentPager();

        FlipperDolphin::Deed(DolphinDeedSubGhzSend);
    }

    void resendSingle(uint32_t) {
        PagerDataStored* pager = getPager();
        subghz->Transmit(protocol->CreatePayload(pager->data, pager->te, config->SignalRepeats));

        FlipperDolphin::Deed(DolphinDeedSubGhzSend);
    }

    void sendAction(PagerAction action) {
        PagerDataStored* pager = getPager();
        subghz->Transmit(protocol->CreatePayload(decoder->SetAction(pager->data, action), pager->te, config->SignalRepeats));

        FlipperDolphin::Deed(DolphinDeedSubGhzSend);
    }

    void sendCurrentPager() {
        PagerDataStored* pager = getPager();
        batchTransmissionScreen->SetProgress(currentPager, config->MaxPagerForBatchOrDetection);
        subghz->Transmit(protocol->CreatePayload(decoder->SetPager(pager->data, currentPager), pager->te, config->SignalRepeats));
    }

    void txComplete() {
        if(transmittingBatch) {
            if(++currentPager <= config->MaxPagerForBatchOrDetection) {
                sendCurrentPager();
                return;
            } else {
                transmittingBatch = false;
                UiManager::GetInstance()->PopView(false);
            }
        }

        subghz->DefaultAfterTransmissionHandler();
    }

    void onReturn() {
        transmittingBatch = false;
    }

    void destroy() {
        subghz->SetTransmitCompleteHandler(NULL);
        for(size_t i = 0; i < decoder->GetSupportedActions().size(); i++) {
            delete actionsStrings[i];
        }
        delete[] actionsStrings;
        delete this;
    }

public:
    UiView* GetView() {
        return submenu;
    }
};
