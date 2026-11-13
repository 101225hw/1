// ============================================================
//  DaCaiBeiLoader.c —— 极简 dlopen loader（arm64e / arm64 通用）
//  作用：被 TrollFools 注入（仅 ~30KB，重签零压力）
//        构造函数里 dlopen 外部的 FridaGadget.dylib（37.8MB，不重签）
//  编译：分两个架构各编一次，再 lipo 合并（见 .github/workflows/build-loader.yml）
// ============================================================
#import <Foundation/Foundation.h>
#import <dlfcn.h>
#import <objc/runtime.h>
#import <unistd.h>

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
    // ⑤ 我们自己的目录（顾客也可以放这里）
    [a addObject:@"/var/mobile/Library/DaCaiBei/FridaGadget.dylib"];

    return a;
}

static void DCBLog(NSString *msg) {
    NSDateFormatter *fmt = [[NSDateFormatter alloc] init];
    fmt.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    NSString *ts = [fmt stringFromDate:[NSDate date]];

    NSString *line = [NSString stringWithFormat:@"[%@] %@\n", ts, msg];

    NSMutableArray *paths = [NSMutableArray array];
    [paths addObject:@"/tmp/dcb_loader.log"];
    NSString *docs = [NSSearchPathForDirectoriesInDomains(
                        NSDocumentDirectory, NSUserDomainMask, YES) firstObject];
    if (docs.length) {
        [paths addObject:[docs stringByAppendingPathComponent:@"dcb_loader.log"]];
    }

    for (NSString *p in paths) {
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
                (int)[[NSProcessInfo processInfo] processIdentifier],
                AppBundlePath()]);
        DCBLog([NSString stringWithFormat:@"iOS=%@ model=%@",
                [[UIDevice currentDevice] systemVersion],
                [[UIDevice currentDevice] model]]);

        NSArray *cands = CandidatePaths();
        BOOL loaded = NO;

        for (NSString *path in cands) {
            DCBLog([NSString stringWithFormat:@"try: %@", path]);
            if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
                DCBLog(@"  -> not found");
                continue;
            }

            NSError *attrErr = nil;
            NSDictionary *attr = [[NSFileManager defaultManager]
                                   attributesOfItemAtPath:path error:&attrErr];
            if (attr) {
                DCBLog([NSString stringWithFormat:@"  size=%llu bytes",
                        [attr fileSize]]);
            }

            void *h = dlopen(path.UTF8String, RTLD_NOW | RTLD_GLOBAL);
            if (h) {
                DCBLog([NSString stringWithFormat:@"  -> dlopen OK (%p)", h]);
                loaded = YES;
                break;      // gadget 构造函数会自动跑
            } else {
                const char *err = dlerror();
                DCBLog([NSString stringWithFormat:@"  -> dlopen FAILED: %s",
                        err ? err : "unknown"]);
            }
        }

        if (loaded) {
            DCBLog(@"=== gadget loaded, waiting for its init ===");
        } else {
            DCBLog(@"=== ALL candidate paths failed ===");
            DCBLog(@"=== 请确认 FridaGadget.dylib 已放入 Frameworks/ 目录 ===");
        }
    }
}
