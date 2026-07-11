#include <Poseidon/UI/Controls/IosKeyboardAccessory.hpp>

#if defined(POSEIDON_TARGET_IOS)

#import <UIKit/UIKit.h>
#include <SDL3/SDL_keyboard.h>

@interface PoseidonKeyboardAccessoryTarget : NSObject
- (void)hideKeyboard:(id)sender;
@end

@implementation PoseidonKeyboardAccessoryTarget
- (void)hideKeyboard:(id)sender
{
    SDL_Window* window = SDL_GetKeyboardFocus();
    if (window != nullptr)
    {
        SDL_StopTextInput(window);
    }
}
@end

namespace
{

PoseidonKeyboardAccessoryTarget* AccessoryTarget()
{
    static PoseidonKeyboardAccessoryTarget* target = [[PoseidonKeyboardAccessoryTarget alloc] init];
    return target;
}

UIWindow* ActiveWindow()
{
    if (@available(iOS 13.0, *))
    {
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
        {
            if (scene.activationState != UISceneActivationStateForegroundActive ||
                ![scene isKindOfClass:UIWindowScene.class])
            {
                continue;
            }

            UIWindowScene* windowScene = (UIWindowScene*)scene;
            for (UIWindow* window in windowScene.windows)
            {
                if (window.isKeyWindow)
                {
                    return window;
                }
            }
        }
    }

    return UIApplication.sharedApplication.keyWindow;
}

UITextField* FindFirstResponderTextField(UIView* view)
{
    if ([view isKindOfClass:UITextField.class] && view.isFirstResponder)
    {
        return (UITextField*)view;
    }

    for (UIView* subview in view.subviews)
    {
        UITextField* field = FindFirstResponderTextField(subview);
        if (field != nil)
        {
            return field;
        }
    }

    return nil;
}

UITextField* FindSdlTextField(UIView* view)
{
    if ([view isKindOfClass:UITextField.class] &&
        [NSStringFromClass(view.class) isEqualToString:@"SDLUITextField"])
    {
        return (UITextField*)view;
    }

    for (UIView* subview in view.subviews)
    {
        UITextField* field = FindSdlTextField(subview);
        if (field != nil)
        {
            return field;
        }
    }

    return nil;
}

void InstallAccessory(UITextField* field)
{
    if (field == nil)
    {
        return;
    }

    UIToolbar* toolbar = [[UIToolbar alloc] initWithFrame:CGRectMake(0.0f, 0.0f, 0.0f, 44.0f)];
    toolbar.autoresizingMask = UIViewAutoresizingFlexibleWidth;

    UIBarButtonItem* spacer =
        [[UIBarButtonItem alloc] initWithBarButtonSystemItem:UIBarButtonSystemItemFlexibleSpace target:nil action:nil];
    UIBarButtonItem* hide =
        [[UIBarButtonItem alloc] initWithTitle:@"Hide Keyboard"
                                         style:UIBarButtonItemStyleDone
                                        target:AccessoryTarget()
                                        action:@selector(hideKeyboard:)];
    toolbar.items = @[ spacer, hide ];
    [toolbar sizeToFit];

    field.inputAccessoryView = toolbar;
    [field reloadInputViews];
}

} // namespace

namespace Poseidon
{

void InstallIosKeyboardHideAccessory()
{
    @autoreleasepool
    {
        UIWindow* window = ActiveWindow();
        if (window == nil)
        {
            return;
        }

        UITextField* field = FindFirstResponderTextField(window);
        if (field == nil)
        {
            field = FindSdlTextField(window);
        }

        InstallAccessory(field);
    }
}

} // namespace Poseidon

#endif
