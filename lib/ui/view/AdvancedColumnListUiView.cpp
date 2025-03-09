#ifndef _ADVANCED_SUBMENU_UI_VIEW_CLASS_
#define _ADVANCED_SUBMENU_UI_VIEW_CLASS_

#include <furi.h>
#include <gui/gui.h>
#include <gui/elements.h>

#include "UiView.cpp"
#include "lib/String.cpp"
#include "lib/HandlerContext.cpp"

#undef LOG_TAG
#define LOG_TAG "UI_ADV_SUBMENU"

using namespace std;

// Inspired by https://github.com/flipperdevices/flipperzero-firmware/blob/dev/applications/main/subghz/views/receiver.c

#define FRAME_HEIGHT    12
#define ITEMS_ON_SCREEN 4

struct AdvSubMenuViewModel {
    void* uiVIew;
};

class AdvancedColumnListUiView : public UiView {
private:
    View* view = NULL;
    const char* noElementsCapton = NULL;

    const char* leftButtonCaption = NULL;
    const char* ceneterButtonCaption = NULL;
    const char* rightButtonCaption = NULL;

    int listOffset = 0;
    int selectedIndex = 0;
    int elementsCount = 0;

    int8_t columnCount;
    int8_t* columnOffsets;
    Font* columnFonts;
    Align* columnAlignments;

    function<void(int, int, String* name)> getColumnElementName;

public:
    AdvancedColumnListUiView(
        int8_t* columnOffsets,
        int8_t columnCount,
        function<void(int, int, String* name)> columnElementNameGetter
    ) {
        this->columnCount = columnCount;
        this->columnOffsets = columnOffsets;
        this->getColumnElementName = columnElementNameGetter;

        view = view_alloc();
        view_set_context(view, this);

        view_set_draw_callback(view, drawCallback);
        view_set_input_callback(view, inputCallback);
        view_set_enter_callback(view, enterCallback);
        view_set_exit_callback(view, exitCallback);

        view_allocate_model(view, ViewModelTypeLockFree, sizeof(AdvancedColumnListUiView*));
        with_view_model_cpp(view, AdvSubMenuViewModel*, model, model->uiVIew = this;, false);
    }

    void SetNoElementCaption(const char* noElementsCapton) {
        this->noElementsCapton = noElementsCapton;
    }

    void SetColumnFonts(Font* columnFonts) {
        this->columnFonts = columnFonts;
    }

    void SetColumnAlignments(Align* columnAlignments) {
        this->columnAlignments = columnAlignments;
    }

    void SetLeftButton(const char* caption) {
        leftButtonCaption = caption;
    }

    void SetCenterButton(const char* caption) {
        ceneterButtonCaption = caption;
    }

    void SetRightButton(const char* caption) {
        rightButtonCaption = caption;
    }

    void AddElement() {
        if(elementsCount == 0 || selectedIndex == elementsCount - 1) {
            setIndex(elementsCount);
        }
        elementsCount++;
    }

    void Refresh() {
        view_commit_model(view, true);
    }

    View* GetNativeView() {
        return view;
    }

    ~AdvancedColumnListUiView() {
        if(view != NULL) {
            OnDestory();
            view_free_model(view);
            view_free(view);
            view = NULL;
        }
    }

private:
    void draw(Canvas* canvas) {
        canvas_clear(canvas);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontSecondary);

        if(leftButtonCaption != NULL) elements_button_left(canvas, leftButtonCaption);
        if(ceneterButtonCaption != NULL) elements_button_center(canvas, ceneterButtonCaption);
        if(rightButtonCaption != NULL) elements_button_right(canvas, rightButtonCaption);

        if(elementsCount == 0 && noElementsCapton != NULL) {
            int wCenter = canvas_width(canvas) / 2;
            int hCenter = canvas_height(canvas) / 2;
            canvas_set_font(canvas, FontPrimary);
            canvas_draw_str_aligned(canvas, wCenter, hCenter, AlignCenter, AlignCenter, noElementsCapton);
            canvas_set_font(canvas, FontSecondary);
        }

        String stringBuffer;
        bool scrollbar = elementsCount > 4;

        for(int i = 0; i < MIN(elementsCount, ITEMS_ON_SCREEN); i++) {
            int idx = CLAMP(i + listOffset, elementsCount, 0);

            if(selectedIndex == idx) {
                canvas_set_color(canvas, ColorBlack);
                canvas_draw_box(canvas, 0, 0 + i * FRAME_HEIGHT, scrollbar ? 122 : 127, FRAME_HEIGHT);

                canvas_set_color(canvas, ColorWhite);
                canvas_draw_dot(canvas, 0, 0 + i * FRAME_HEIGHT);
                canvas_draw_dot(canvas, 1, 0 + i * FRAME_HEIGHT);
                canvas_draw_dot(canvas, 0, (0 + i * FRAME_HEIGHT) + 1);

                canvas_draw_dot(canvas, 0, (0 + i * FRAME_HEIGHT) + 11);
                canvas_draw_dot(canvas, scrollbar ? 121 : 126, 0 + i * FRAME_HEIGHT);
                canvas_draw_dot(canvas, scrollbar ? 121 : 126, (0 + i * FRAME_HEIGHT) + 11);
            } else {
                canvas_set_color(canvas, ColorBlack);
            }

            for(int8_t column = 0; column < columnCount; column++) {
                if(columnFonts != NULL) {
                    canvas_set_font(canvas, columnFonts[column]);
                }

                int8_t columnOffset = columnOffsets[column];
                getColumnElementName(idx, column, &stringBuffer);
                // elements_string_fit_width(canvas, stringBuffer.furiString(), maxWidth);
                if(columnAlignments == NULL) {
                    canvas_draw_str(canvas, columnOffset, 9 + i * FRAME_HEIGHT, stringBuffer.cstr());
                } else {
                    canvas_draw_str_aligned(
                        canvas, columnOffset, 9 + i * FRAME_HEIGHT, columnAlignments[column], AlignBottom, stringBuffer.cstr()
                    );
                }

                canvas_set_font(canvas, FontSecondary);

                stringBuffer.Reset();
            }
        }

        if(scrollbar) {
            elements_scrollbar_pos(canvas, 128, 0, 49, selectedIndex, elementsCount);
        }
    }

    bool input(InputEvent* event) {
        if(event->type != InputTypeShort) {
            return false;
        }

        switch(event->key) {
        case InputKeyUp:
            if(selectedIndex == 0) {
                setIndex(elementsCount - 1);
            } else {
                setIndex(selectedIndex - 1);
            }
            return true;

        case InputKeyDown:
            if(selectedIndex >= elementsCount - 1) {
                setIndex(0);
            } else {
                setIndex(selectedIndex + 1);
            }
            return true;

        default:
            break;
        }

        return false;
    }

    void setIndex(int index) {
        selectedIndex = index;

        // if(selectedIndex > 2) {
        //     listOffset = selectedIndex - 2;
        // }

        int bounds = elementsCount > 3 ? 2 : elementsCount;
        if(elementsCount > 3 && selectedIndex >= elementsCount - 1) {
            listOffset = selectedIndex - 3;
        } else if(listOffset < selectedIndex - bounds) {
            listOffset = CLAMP(listOffset + 1, elementsCount - bounds, 0);
        } else if(listOffset > selectedIndex - bounds) {
            listOffset = CLAMP(selectedIndex - 1, elementsCount - bounds, 0);
        }
    }

    static void drawCallback(Canvas* canvas, void* model) {
        AdvancedColumnListUiView* uiView = (AdvancedColumnListUiView*)((AdvSubMenuViewModel*)model)->uiVIew;
        uiView->draw(canvas);
    }

    static bool inputCallback(InputEvent* event, void* context) {
        AdvancedColumnListUiView* uiView = (AdvancedColumnListUiView*)context;
        if(uiView->input(event)) {
            uiView->Refresh();
            return true;
        }
        return false;
    }

    static void enterCallback(void* context) {
        UNUSED(context);
    }

    static void exitCallback(void* context) {
        UNUSED(context);
    }
};

#endif //_ADVANCED_SUBMENU_UI_VIEW_CLASS_
