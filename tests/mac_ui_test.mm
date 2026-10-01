#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include "app_ui.h"

namespace
{
    NSButton* FindButton(NSView* view, NSString* title)
    {
        if ([view isKindOfClass:[NSButton class]])
        {
            NSButton* button = (NSButton*)view;
            if ([button.title isEqualToString:title])
                return button;
        }
        for (NSView* child in view.subviews)
        {
            NSButton* found = FindButton(child, title);
            if (found)
                return found;
        }
        return nil;
    }
}

@interface AutoSZUWebMacAlertTestDriver : NSObject
@property(nonatomic, retain) NSString* buttonTitle;
@property(nonatomic, assign) BOOL clicked;
@property(nonatomic, assign) NSInteger attempts;
@end

@implementation AutoSZUWebMacAlertTestDriver

- (void)dealloc
{
    [_buttonTitle release];
    [super dealloc];
}

- (void)tryClick:(NSTimer*)timer
{
    NSWindow* window = NSApp.modalWindow;
    NSButton* button = window ? FindButton(window.contentView, self.buttonTitle) : nil;
    if (button)
    {
        self.clicked = YES;
        [timer invalidate];
        [button performClick:nil];
        return;
    }

    self.attempts += 1;
    if (self.attempts >= 40)
    {
        [timer invalidate];
        [NSApp abortModal];
    }
}

@end

static bool RunButtonDismissTest(NSString* title,
                                 bool allowCancel,
                                 AppUI::Button expected)
{
    AutoSZUWebMacAlertTestDriver* driver =
        [[AutoSZUWebMacAlertTestDriver alloc] init];
    driver.buttonTitle = title;

    NSTimer* timer = [NSTimer timerWithTimeInterval:0.05
        target:driver
        selector:@selector(tryClick:)
        userInfo:nil
        repeats:YES];
    [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSModalPanelRunLoopMode];

    const AppUI::Button result = AppUI::ShowMessage(
        "macOS modal button regression test", "AutoSZUWeb Test", allowCancel);
    const bool passed = driver.clicked && result == expected;
    [driver release];
    return passed;
}

int main()
{
    @autoreleasepool
    {
        [NSApplication sharedApplication];
        [NSApp finishLaunching];

        if (!RunButtonDismissTest(@"确定", false, AppUI::Button::Ok))
            return 1;
        if (!RunButtonDismissTest(@"取消", true, AppUI::Button::Cancel))
            return 2;
        return 0;
    }
}