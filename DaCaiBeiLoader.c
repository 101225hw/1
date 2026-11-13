// ============================================================
//  DaCaiBeiLoader.c —— 极简 dlopen loader（arm64e / arm64 通用）
//  作用：被 TrollFools 注入（仅 ~15KB，重签零压力）
//        构造函数里 dlopen 外部的 FridaGadget.dylib（37.8MB，不重签）
//  编译：clang -arch arm64e -arch arm64 -dynamiclib -o DaCaiBeiLoader.dylib
//        -framework Foundation -mios-version-min=14.0 -fobjc-arc
// ============================================================
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <objc/runtime.h>

// 候选路径：gadget 放在哪就从哪加载（覆盖 TrollFools / TrollStore / 越狱三种布局）
static NSString *const kGadgetName = @"FridaGadget.dylib";

static NSString *AppBundlePath(void) {
    // 主 bundle 路径，例如 /var/containers/Bundle/Application/<UUID>/ProductName.app
    NSString *p = [[NSBundle mainBundle] bundlePath];
    if (p.length) return p;
    // 退化：从可执行路径推
    NSString *exe = [[NSProcessInfo processInfo] arguments].firstObject;
    return [exe stringByDeletingLastPathComponent];
}

static NSArray<NSString *> *CandidatePaths(void) {
    NSString *app = AppBundlePath();
    NSMutableArray *a = [NSMutableArray array];

    // ① 与 loader 同目录（TrollFools 把两者都放 Frameworks/）
    Dl_info info;
    if (dladdr((__bridge void *)CandidatePaths, &info) && info.dli_fname) {
        NSString *selfPath = [NSString stringWithUTF8String:info.dli_fname];
        [a addObject:[[selfPath stringByDeletingLastPathComponent]
                       stringByAppendingPathComponent:kGadgetName]];
    }

    // ② App 根目录
    [a addObject:[app stringByAppendingPathComponent:kGadgetName]];
    // ③ Frameworks/
    [a addObject:[[app stringByAppendingPathComponent:@"Frameworks"]
                   stringByAppendingPathComponent:kGadgetName]];
    // ④ 越狱标准位置
    [a addObject:@"/usr/lib/dacaibei/FridaGadget.dylib"];
    [a addObject:@"/Library/MobileSubstrate/DynamicLibraries/FridaGadget.dylib"];

    return a;
}

static void DCBLog(NSString *msg) {
    NSMutableString *line = [NSMutableString stringWithFormat:
        @"[%@] %@\n", [NSDate date], msg];

    NSMutableArray *pathes = [NSMutableArray array];
    [pathes addObject:@"/tmp/dcb_loader.log"];
    NSString *docs = [NSSearchPathForDirectoriesInDomains(
                        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (docs.length) {
        [pathes addObject:[docs stringByAppendingPathComponent:@"dcb_loader.log"]];
    }

    for (NSString *p in pathes) {
        FILE *f = fopen(p.UTF8String, "a");
        if (f) {
            fputs(line.UTF8String, f);
            fclose(f);
        }
    }
    NSLog(@"[DCBLoader] %@", msg);
}

__attribute__((constructor))
static void DCBLoaderInit(void) {
    @autoreleasepool {
        DCBLog(@"=== DCBLoader init ===");
        DCBLog([NSString stringWithFormat:@"pid=%d bundle=%@",
                getpid(), AppBundlePath()]);

        NSArray *cands = CandidatePaths();
        for (NSString *path in cands) {
            DCBLog([NSString stringWithFormat:@"try: %@", path]);
            BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:path];
            if (!exists) {
                DCBLog(@"  → 不存在");
                continue;
            }

            void *h = dlopen(path.UTF8String, RTLD_NOW | RTLD_GLOBAL);
            if (h) {
                DCBLog([NSString stringWithFormat:@"  → ✓ dlopen 成功 (%p)", h]);
                return;     // gadget 的构造函数会自动跑
            } else {
                const char *err = dlerror();
                DCBLog([NSString stringWithFormat:@"  → ✗ dlopen 失败: %s",
                        err ? err : "unknown"]);
            }
        }
        DCBLog(@"=== 所有候选路径都失败 ===");
    }
}
