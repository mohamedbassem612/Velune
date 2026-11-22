# Velune Desktop

Phase 2 playback setup, timing, and benchmarks are documented in
[Playback Phase 2](#playback-phase-2--libmpv-and-measurements),
[benchmark results](benchmarks/RESULTS.md), and
[Windows deployment](WINDOWS_DEPLOYMENT.md). Qt remains the default backend.

واجهة Windows/Linux مستقلة بلغة **C++20 / Qt 6 Widgets**. تطبيق Android الحالي يظل مستقلًا.

التصميم يتبع الصورة المرجعية: AMOLED أسود، قائمة جانبية ثابتة، شبكة 3×3 بالترتيب المحدد، ثلاثة كروت، ومشغل ثابت بارتفاع 128px. خانة **Liked Songs** ثابتة أولًا بصورتها قلب أبيض مصمت. شريط التقديم في منتصف المشغل تحت أزرار التشغيل. الأيقونات والموجات مرسومة داخل Qt بدون صور زخرفية خارجية. ألوان الواجهة مجمعة في `src/ui/Theme.h`.

## التشغيل والبناء

المتطلبات: CMake 3.21+، مترجم C++20، OpenSSL، Qt 6.4+ مع Widgets / Network / Multimedia / WebEngineWidgets / Test، ومكتبة Qt6Keychain لحفظ الجلسة في مخزن النظام.

Ubuntu/Debian:

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-multimedia-dev qt6-webengine-dev qtkeychain-qt6-dev libssl-dev fonts-inter
cmake -S desktop -B desktop/build -DCMAKE_BUILD_TYPE=Release
cmake --build desktop/build -j4
python3 desktop/scripts/install-playback-tools.py desktop/build/tools
ctest --test-dir desktop/build --output-on-failure
desktop/build/velune
```

Windows: استخدم MSVC x64 مع حزم Qt المتوافقة التي تدعم Qt WebEngine، وOpenSSL وQt6Keychain المطابقين. تعليمات البناء والتغليف و`windeployqt` وحالة التحقق في [WINDOWS_DEPLOYMENT.md](WINDOWS_DEPLOYMENT.md). لا يوجد تحقق تشغيل Windows محلي في بيئة Linux الحالية.

يمكن تمرير `-DVELUNE_MINIMAL_BUILD=ON` للعمل دون الصوت المحلي أو حفظ الجلسة إذا لم تتوفر اعتماداتهما. تسجيل الدخول المضمّن يتطلب Qt WebEngineWidgets حتى في البناء المصغّر. النسخة المثبتة مبنية بالصوت وQt6Keychain.

## التثبيت لحساب Linux الحالي

```sh
desktop/scripts/install-linux.sh desktop/build
```

التثبيت في `~/.local/lib/velune`، وأمر التشغيل `~/.local/bin/velune`. تظهر قائمتان في التطبيقات:

- **Velune**: يبدأ بشاشة مموّهة ويتطلب جلسة Google صحيحة.
- **Velune — UI Preview**: يعرض التصميم مباشرة في ملف بيانات منفصل، دون إنشاء تسجيل دخول أو بيانات موسيقى وهمية. الأزرار تستخدم الخدمات الفعلية، ويمكن استيراد ملفات صوت من Settings.

يقبل سكربت التثبيت مسارًا ثانيًا اختياريًا لجذر حزم Qt المستخرجة، لتجهيز المكتبات محليًا دون صلاحيات administrator. لا يغيّر مكتبات النظام ولا يعطل TLS أو حماية المتصفح.

حذف التثبيت: احذف `~/.local/lib/velune` و`~/.local/bin/velune` ومدخلي `velune*.desktop` من `~/.local/share/applications`. بيانات المكتبة منفصلة ولا يحذفها المثبّت.

## صفحات المجموعات والتفاعل

- البحث أثناء الكتابة يستخدم `music/get_search_suggestions` بعد 200ms، ويعرض سجل البحث والاقتراحات النصية والأغاني والألبومات والفنانين. Enter أو اختيار اقتراح ينفّذ البحث الكامل.
- البحث الكامل يعرض Top result والأقسام التي تعيدها الخدمة، مع قراءة card/list/carousel وitemSectionRenderer. الفلاتر المنقولة من Android: Songs وVideos وAlbums وArtists وFeatured playlists وCommunity playlists. صفحات الفلاتر تدعم continuation وتحميل المزيد مع إزالة التكرار، وإعادة المحاولة عند الفشل، وإلغاء الردود القديمة. ملخص ناقص الأقسام يستخدم fallback Songs/Albums بحجم 12 مثل Android.
- Search preferences يتيح لغة وبلد المحتوى، إخفاء explicit والفيديوهات، استخدام الحساب في البحث الكامل، إيقاف السجل ومسحه. الإعدادات والسجل محفوظان محليًا في `search.json` كتابةً ذرّية؛ السجل لا يتزامن تلقائيًا مع قاعدة بيانات الهاتف. إزالة مدخل منفرد من قائمة الاقتراحات متاحة بالنقر بالزر الأيمن.
- نطاق Library يبحث في الأغاني والألبومات والفنانين والبلايليستات المتاحة في مكتبة Desktop. Offline files يقصر النطاق على الملفات المحلية المسجلة والموجودة؛ لا يفترض وجود cache Android أو بيانات لم تُنقل. metadata المحتوى القديمة قد تفتقد explicit/video markers حتى يعاد استيرادها. الانتقال إلى وضع Offline files يدوي.
- منطق البحث مستقل في `src/services/SearchEngine.*`، مع اختبارات نقل الفلاتر والتصنيفات والـpagination والسجل والإلغاء وردود الخدمة المتأخرة في `tests/SearchTests.cpp`، واختبارات الربط بالواجهة في `tests/CoreTests.cpp`.
- نتائج البحث والمكتبة وQueue تضيء عند مرور المؤشر، ويظهر زر Play فوق الغلاف؛ الضغط على بقية الصف يفتح التفاصيل. Enter يفتح التفاصيل وCtrl+Enter يشغّل العنصر المحدد.
- كروت Home تعرض صورة وعنوانًا ووصفًا. زر Play يظهر مع المرور بالمؤشر أو التركيز بلوحة المفاتيح؛ الضغط عليه يبدأ التشغيل، والضغط على بقية الكارت يفتح التفاصيل دون تغيير التشغيل الحالي.
- صفحات Liked Songs وDaily Mix والراديو والألبومات تستخدم غلافًا بجانب العنوان، ثم قائمة أغاني مع الإعجاب والخيارات. القلب المفرغ يعني أن الأغنية غير معجب بها. المشغّل ثابت أثناء تمرير القائمة.
- زر Play في المشغّل ظاهر دائمًا، ويتحول إلى خلفية بيضاء وأيقونة سوداء عند مرور المؤشر.
- Daily Mix 1 و2 مجموعتان محليتان من المكتبة، بترتيب ثابت خلال اليوم وحد أقصى 30 أغنية؛ ليستا قوائم توصيات مستوردة من YouTube Music. الواجهة توضح ذلك بعبارة From your library.
- فتح الراديو يجلب أغانيه دون تشغيلها. فتح الأغنية يجلب الألبوم عند توفر album browse ID؛ التسجيلات القديمة والملفات التي تفتقد معلومات الألبوم تفتح صفحة الأغنية. اكتمال الألبوم يعتمد على استجابة الخدمة الحالية، ولا توجد pagination للألبومات بعد.

## Library على Desktop

زر Library تحت Search يفتح نفس تقسيم مكتبة Android: عرض مختلط افتراضي، وفلاتر Songs / Albums / Artists / Playlists. يظهر Liked Songs بقلب أبيض، ومعه Offline وMy Top وCached؛ يمكن إخفاء هذه القوائم وتغيير حجم My Top من إعدادات Library.

- عرض List أو Grid بحجمين، وترتيب بالاسم أو الإضافة وبحسب نوع المحتوى: الفنان، السنة، العدد، المدة أو وقت الاستماع. لكل قسم فلتر وترتيب محفوظان.
- الأغاني: Liked / Library / Offline؛ الألبومات: Liked / Library / Offline / Fully offline؛ الفنانون: Liked / Library. العرض المختلط يجمع الألبومات والفنانين المحفوظين والبلايليستات، دون تحويل كل فنان موجود في بيانات أغنية إلى فنان محفوظ.
- Refresh يستخدم مصادر Android نفسها: `VLLM` و`FEmusic_liked_videos` و`FEmusic_liked_albums` و`FEmusic_library_corpus_artists` و`FEmusic_liked_playlists`، مع متابعة الصفحات وإبقاء البيانات السابقة عند الفشل. الألبومات تُستكمل من قائمة التشغيل الخاصة بها؛ Fully offline يتطلب قائمة أغاني كاملة وملفات فعلية لكل الأغاني.
- قائمة الزر الأيمن تتيح حفظ الألبوم أو الفنان، وإنشاء قوائم محلية وإضافة الأغاني وتسميتها وحذفها، وتاجات القوائم. يمكن سحب القوائم لإعادة ترتيبها عند اختيار Playlists / Custom order / List دون فلتر تاجات.
- History وListening stats وMy Top تعتمد على أحداث الاستماع الفعلية أو التاريخ المستورد عبر Connect؛ التقديم في الأغنية لا يُحسب وقت استماع. القوائم المحلية تدخل snapshot المكتبة؛ إعدادات العرض والتاجات وحفظ الألبومات والفنانين محفوظة محليًا في `catalog.json` ولا تتزامن مع قاعدة بيانات الهاتف تلقائيًا. حفظ عنصر هنا لا يعدّل حساب YouTube Music.
- Offline يتحقق من الملفات المسجلة الموجودة والقابلة للقراءة. Cached فارغ حاليًا لأن Desktop لا يحتفظ بملفات بث دائمة؛ لا يُفترض وجود تنزيلات أو cache Android على الكمبيوتر.

## الوظائف الموجودة

- التنقل والبحث وExplore والمكتبة وQueue، مفضلة محلية، shuffle/repeat، تقديم/تأخير، مستوى الصوت وmute، fullscreen، وأزرار النافذة مع السحب وتغيير الحجم.
- التركيز بلوحة المفاتيح، `Space` للتشغيل والإيقاف، `Ctrl+F` للبحث، و`F11` لملء الشاشة. السلايدر يدعم السحب والأسهم.
- تسجيل Google داخل نافذة **YouTube Music مضمّنة في Velune** باستخدام Qt WebEngine، بنفس أسلوب جلسة Android. لا يحتاج OAuth JSON أو إعداد Google Cloud. يتحقق من الحساب عبر `account/account_menu` قبل فتح الواجهة.
- ملف المتصفح في الذاكرة فقط، بدون حفظ جلسة Chromium على القرص. تُحفظ كوكيز YouTube اللازمة فقط في مخزن كلمات مرور النظام عبر Qt6Keychain، مع منع الحفظ النصي الاحتياطي. عند عدم توفر المخزن الآمن تبقى الجلسة في الذاكرة فقط. Sign out يمسح جلسة Velune المحلية.
- تحديث الأغاني والألبومات والفنانين والبلايليستات المحفوظة من مصادر مكتبة YouTube Music مع pagination. [طريقة تسجيل الدخول وحدوده](GOOGLE_SIGN_IN.md).
- البحث وExplore وSong Radio يستخدمون طلبات الخدمة الفعلية، مع إظهار الأخطاء. الشبكة تعتمد على نفس WEB_REMIX وإعداداته الحالية في المشروع.
- استيراد وتشغيل الصوت المحلي باستخدام Qt Multimedia. تشغيل YouTube يستخدم `yt-dlp` مع Deno لاستخراج مصدر بث، ويبدأ الصوت دون انتظار تنزيل ملف الأغنية كاملًا. مسار سريع من YouTube Music للأغاني العامة، ومسار بديل بجلسة الحساب عند الحاجة. قد يتضمن المصدر مسار فيديو؛ يحتفظ المشغّل بمخرج Qt صالح ويعطّل مسار الفيديو بعد التهيئة، مع بقاء الواجهة الأصلية والصوت فقط.
- خدمة بث HTTP محلية على `127.0.0.1` في خيط منفصل، تدعم byte ranges لتقديم الصوت للأمام والخلف، وتُبقي ترويسات المصدر اللازمة. الاتصال الخارجي محصور في HTTPS لنطاقات Googlevideo، بدون إرسال كوكيز الحساب إلى CDN أو اتباع إعادة توجيه. البيانات تتدفق بذاكرة محدودة ولا تُحفظ كملف أغنية كامل.
- حفظ روابط المصادر الصالحة في الذاكرة خمس دقائق، منفصلة حسب جلسة الحساب؛ وتجهيز رابط الأغنية التالية في الخلفية عند التشغيل بالتتابع. تغيير الأغنية يلغي الطلب السابق، ورفض المصدر يتيح محاولة تحديث واحدة. أول تشغيل يتأثر بالشبكة والخدمة؛ وصل الاختبار الحي إلى بدء الصوت خلال حوالي 5 ثوانٍ.
- شريط التقديم يقفز مباشرة إلى موضع الضغط، ويدعم السحب وأسهم الكيبورد بخطوة خمس ثوانٍ، مع حفظ طلب التقديم حتى يصبح المصدر قابلًا له.
- حفظ المكتبة atomically، واستيراد جماعي، ودمج likes/playlists/history بإصدارات Lamport وحفظ tombstones للتعامل مع الحذف والتعارض.

## أدوات تشغيل YouTube

التطبيق مكتوب بـC++؛ محلّل الروابط مساعد خارجي `yt-dlp` مع Deno. سكربت `install-playback-tools.py` ينزّل نسخ Linux x86_64 الرسمية المحددة داخل السكربت ويتحقق من SHA-256 قبل التثبيت في `build/tools`. سكربت تثبيت Linux ينسخ هذه الأدوات إلى مجلد التطبيق. لا يجري تنزيل كود أو تحديث مساعد تلقائيًا أثناء التشغيل.

يمكن أيضًا وضع `yt-dlp` و`deno` في PATH، أو بجانب التطبيق داخل `tools/` (امتداد `.exe` على Windows). تجهيز Windows يحتاج البنيات الرسمية المطابقة؛ سكربت التنزيل الحالي مخصص لـLinux x86_64. لتحديث الأدوات يُراجع إصدارها وقيم SHA-256 في السكربت ثم يعاد تشغيله والتثبيت.

لكل طلب تحليل مجلد مؤقت خاص ومهلة دقيقتين للمحاولة. المسار العام السريع لا يحتاج إرسال جلسة الحساب؛ عند الحاجة للمسار البديل تُصدَّر كوكيز YouTube اللازمة فقط إلى ملف مؤقت بصلاحيات `600` داخل مجلد `700`. يُحذف الملف عند اكتمال التحليل أو الإلغاء والفشل. لا تُرسل الأسرار في معاملات العملية أو رسائل الخطأ. يحتفظ `yt-dlp` بذاكرة تحليل مؤقتة داخل CacheLocation الخاص بالتطبيق، ولا يقرأ ملفات المتصفح الشخصي.

مرجع المساعد: [yt-dlp](https://github.com/yt-dlp/yt-dlp)، [متطلبات JavaScript](https://github.com/yt-dlp/yt-dlp/wiki/EJS).

## Velune Connect

تم الرجوع إلى `docs/VELUNE_CONNECT.md` و`docs/CONNECT_MEDIA.md` والكود الحالي، وليس ملاحظات pairing القديمة:

- اكتشاف IPv4 mDNS لـ`_velune-player._tcp.local`؛ قبول protocol 4 فقط.
- عميل TLS 1.2+ بشهادة RSA محلية، وتأكيد SHA-256 للـSPKI مطابق لهوية discovery، واتصال بعناوين LAN فقط. المفتاح المحلي ملف بصلاحيات owner-only على Linux؛ ليس Android Keystore.
- هوية/تسلسل JSON المحددان في Android، وحد أقصى 12 MiB للإطار، heartbeat، timeout، وإعادة اتصال بتأخير متدرج مع jitter. فصل المستخدم يوقف إعادة الاتصال.
- الاتصال يرفق بالجلسة الحالية مع `startPlayback:false`. لا يستبدل صف التشغيل القديم جلسة التلفزيون عند إعادة الاتصال. التحكم محجوب حتى وصول state حديثة من المنسق.
- أوامر التشغيل وQueue وشريط التقديم وshuffle/repeat وصوت التلفزيون تحمل sessionId/epoch وcommandId، مع معالجة ACK والمهلة ورفض state أقدم.
- تبادل مكتبة version 1 ثنائي الاتجاه مع ACK وإعادة إرسال غير المؤكد. يستخدم هذا الإصدار snapshots كاملة، وليس delta محسّنة.
- وحدات media RPC للملفات المحلية المسجلة: open/read/close، حد 4 قراء، 64 KiB للقراءة، byte offsets/ranges، وإغلاق عند الإلغاء أو انقطاع الاتصال. لا يعلن التطبيق `mediaVersion` حتى يكتمل تحميل cache لكتالوج YouTube، كي يحتفظ التلفزيون بمصدر صوته المستقل بدل الاعتماد على provider لا يوفر كل الأغاني. لا يعلن `assetVersion` لأن lyrics/cover assets لم تُنفّذ بعد.

## حدود هذا الإصدار التي تحتاج استكمالًا

هذه بداية تطبيق Desktop وليست نقلًا كاملًا لكل مزايا Android. المتبقي: منسق Connect/إعلان Desktop كجهاز renderer، انتقال الملكية prepare/release/commit إلى الكمبيوتر، media consumer وتحميل cache من الهاتف، assets/lyrics، تعديل مكتبة حساب Google من Desktop، تنزيلات الخلفية، تحديثات التطبيق وتكاملات Android الأخرى. طلب نقل ملكية الصوت إلى الكمبيوتر يفشل صراحةً؛ لا يرد التطبيق READY قبل وجود renderer قادر على التشغيل.

إصدار 0.1.4 يستبدل تجهيز الملف كاملًا بالبث المباشر، ويصلح التقديم من الواجهة. إصدار 0.1.2 أعاد تسجيل الدخول المضمّن وأزال مسار OAuth من التطبيق. قد ترفض Google المتصفح المضمّن؛ لا يزوّر التطبيق هوية المتصفح ولا يتجاوز الرفض. فتح نافذة تسجيل الدخول أو وجود كوكي وحده لا يُعد تسجيلًا ناجحًا؛ يتطلب تأكيد الحساب من YouTube Music. لم تُجرَّب جلسة Google حقيقية في التحقق الآلي.

## التحقق

اختبارات Qt تغطي: تجزئة الإطارات وحد الحجم ورفض إعادة التسلسل، عناوين LAN، حذف/تعارض المكتبة وثبات الهوية عبر التشغيل، **TLS loopback حقيقي** ورسائل الهوية والجلسة وأوامر ACK وقراءة media range/EOF، وحالة تسجيل الخروج وشبكة 3×3 وثلاثة كروت ومكان شريط التقديم عند 1660×938 و1280×714 و1000×680.

تغطي اختبارات الجلسة عزل نطاقات الكوكيز، انتهاء الصلاحية، رفض حقن الترويسات، توقيع SAPISIDHASH، ومنع إرسال بيانات الحساب إلى أي مضيف أو مسار خارج YouTube Music API. تُختبر نافذة الدخول المضمّنة، قبول هوية الحساب من رد الخدمة، رفض الرد دون هوية، وتجاهل الرد بعد الإلغاء بخادم ردود محاكى؛ دون إدخال حساب حقيقي أو استخدام مخزن كلمات مرور المستخدم.

اختبارات محلّل الصوت تغطي إلغاء العملية وإعادة الطلب، حذف ملف الجلسة، cache للروابط، ورفض المصادر الخارجية وعدم عرض أسرار تشخيص المساعد. اختبارات البث تستعمل HTTP loopback حقيقيًا وتتحقق من نقل byte ranges ومنع تمرير Cookie/Authorization إلى CDN وإغلاق الخدمة. فُحص التشغيل الحقيقي للأغنية الظاهرة في بلاغ المشكلة بشكل مكتوم، بما في ذلك تقدم الوقت والمدة والتقديم للأمام والخلف أثناء استمرار الصوت. اختبارات الواجهة تشغّل ملفًا صامتًا وتختبر الضغط والسحب والكيبورد أثناء التشغيل. هذه الاختبارات لا تستخدم حساب Google الحقيقي. لإعادة الفحص الحي صراحةً:

```sh
VELUNE_LIVE_PLAYBACK_ID=VIDEO_ID desktop/build/velune_audio_tests -platform offscreen
```

التقاط نسخة الواجهة للمراجعة دون حساب:

```sh
desktop/build/velune --preview --screenshot /tmp/velune-home.png --data-dir /tmp/velune-ui-review
```

## Playback Engine — Phase 1

Local audio now passes through `src/playback/IPlaybackEngine.h`. `MusicService` owns the
engine and retains the queue, shuffle/repeat rules, library lookup, YouTube resolution,
prefetch, source refresh, and Connect routing. The UI still receives the same public
methods, Qt signals, and playback JSON. `ConnectClient` and protocol 4 are unchanged.

`QtPlaybackEngine` hides Qt Multimedia behind a private implementation. It owns one
`QMediaPlayer`/`QAudioOutput` pair, disables video using the existing Qt 6.4 workaround,
and translates native notifications into backend-independent playback states and
request-tagged position, duration, seekability, buffering, error, and end events.
An owned engine can be injected through the additional MusicService constructor;
a future Mpv engine can implement this interface without changing the UI or Connect.
No libmpv or extra decoder is included in Phase 1.

### Engine contract and lifecycle

- Operations run on the owning QObject thread. Positions and durations use milliseconds;
  volume is normalized to `[0,1]`, initially `0.75`. MusicService retains its public
  integer percentage volume interface.
- `load(url, request)` replaces the source and clears a pending seek. Supported media
  includes local-file URLs and URLs supported by the Qt backend. Resolution and the
  existing header-aware loopback relay remain outside the engine.
- `play`/`resume`, `pause`, `stop`, and `cancel` have separate meanings. Engine stop
  retains the source for explicit resume; cancel invalidates callbacks and clears it.
  MusicService's `action("stop")` cancels preparation and playback. Repeated controls
  do not create another player. A seek waits until the source becomes seekable,
  retaining the latest target without starting paused playback.
- States are unavailable, idle, loading, playing, paused, stopped, ended, and error.
  Buffer progress is reported separately and does not redefine the UI's playing flag.
- Track replacement, stop, logout, route changes, and destruction invalidate the
  playback generation before cleanup. Resolver handlers and deferred end transitions
  check that generation. Each relay attempt carries its own identifier across the
  worker boundary, so errors queued before replacement or refresh are ignored.
- End events advance once per request. Repeat-one reloads the same prepared URL under
  a new request identity; it does not rerun YouTube extraction. Terminal errors leave
  application state non-playing and allow explicit retry. The existing one-time relay
  source refresh preserves the seek target and play/pause intent.
- Remote selection stops and unloads local audio while the connection is being
  established. Offline targets remain selected and cannot fall through to local audio.
  Detaching restores the local checkpoint paused; explicit resume loads a fresh local
  request. Existing session IDs, epochs, command IDs, ACKs, and reconnect rules remain
  owned by ConnectClient.
- Resolver cancellation kills the retired helper asynchronously, isolates its callbacks,
  and removes temporary credentials. The existing relay worker remains; no engine
  thread, polling loop, background helper, or re-encoding stage has been added.

When Multimedia is absent, the same backend class provides an unavailable implementation:
local play reports the missing backend without resolving media; remote control remains
available. Minimal mode continues to use Multimedia/Keychain if those packages are found.
To explicitly test their absence in a **fresh build directory**:

```sh
cmake -S desktop -B desktop/build-minimal -DVELUNE_MINIMAL_BUILD=ON \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Multimedia=TRUE \
  -DCMAKE_DISABLE_FIND_PACKAGE_Qt6Keychain=TRUE
cmake --build desktop/build-minimal -j2
ctest --test-dir desktop/build-minimal --output-on-failure
```

### Automated validation

The `playback` test target injects a fake engine and uses local fixtures: controls,
paused seeking, volume bounds, replacement, queued stale engine/resolver/relay events,
end/repeat rules, refresh, errors, unavailable playback, remote selection, logout,
shutdown, and reentrant selection. The TLS loopback test exercises MusicService remote
routing with the existing identity/session/ACK and media protocol checks. Relay tests
verify request identity as well as byte ranges and credential isolation. These tests
need neither live YouTube nor audio hardware. The existing Desktop UI integration test
still uses a muted local WAV; live YouTube validation remains explicitly opt-in.

For headless environments where Qt 6.4 WebEngine crashes in its GPU path, run tests with
`QTWEBENGINE_CHROMIUM_FLAGS=--disable-gpu`. This is a test environment setting, not an
application authentication or browser setting change. Qt runtime libraries, plugins,
WebEngineProcess, resources, and locales must also be discoverable; installations with
staged dependencies need the same runtime paths as the Linux launcher.

Phase 2 can add Mpv behind this interface, expand native Windows packaging/build checks,
and validate real remote devices and backend-specific codec behavior. Phase 1 adds no
JVM, Android, Python runtime, or new playback dependency. Existing optional tool-install
scripts and the existing yt-dlp/Deno resolution path are unchanged.

Phase 1 validation in the available Linux environment (Qt 6.4.2 / GCC 13): fresh
Release full and Debug minimal builds succeeded; all five CTest targets passed in
each configuration, with the live YouTube check intentionally skipped. The minimal
build explicitly disabled Multimedia and Keychain discovery. Tests used staged Qt
runtime paths and the headless WebEngine GPU setting above. No Windows build,
real remote-device session, or live YouTube playback was performed for this refactor.

## Playback Phase 2 — libmpv and measurements

`MpvPlaybackEngine` implements the existing request-tagged `IPlaybackEngine`
contract. `MusicService` retains ownership of the queue, resolver, relay,
prefetch/cache, refresh, and Connect routing. No Connect protocol or Android/TV
message changes are made. Qt Multimedia remains the default and alternative.

```sh
# Optional Linux backend dependency; minimal/Qt builds do not require it.
sudo apt install libmpv-dev
cmake -S desktop -B desktop/build -DCMAKE_BUILD_TYPE=Release \
  -DVELUNE_BUILD_BENCHMARKS=ON
cmake --build desktop/build -j2
VELUNE_PLAYBACK_BACKEND=qt desktop/build/velune
VELUNE_PLAYBACK_BACKEND=mpv desktop/build/velune
```

`VELUNE_ENABLE_MPV=ON` discovers official development headers, optionally via
`MPV_ROOT`. Headers need client API 2.2+ (mpv 0.37+); the runtime must have the
compatible major API and required exports. A missing/incompatible runtime or
initialization failure reports a diagnostic and falls back to available Qt.
Missing headers or `-DVELUNE_ENABLE_MPV=OFF` produces a build without mpv.
Neither backend available leaves remote control usable and local play reports
the missing dependency without starting extraction. Minimal mode still uses
optional packages when discovered; explicitly disable them to test their absence.

Before constructing Qt audio objects, Velune checks multimedia plugin metadata
and loadability through public QtCore APIs. Missing plugins or dependent DLLs
produce an unavailable engine and a request-tagged installation diagnostic,
including on Qt 6.4, which otherwise aborts during construction. Explicit
`QT_MEDIA_BACKEND` is respected. Without it, all discovered candidates must load;
Velune then leaves default selection to Qt. A broken unused candidate can thus
conservatively disable local Qt playback; select a known working backend explicitly
to avoid that case. Static plugin metadata is supported but was not runtime-tested.
This cannot diagnose every failure inside a loadable plugin's integration factory.

Linux loads `libmpv.so.2` through the native loader. Windows loads an audited
`mpv-2.dll` or `libmpv-2.dll` beside the executable. Runtime loading avoids a
mandatory loader/import dependency, including for Qt fallback; there is no
runtime dependency download. Media errors do not trigger a second backend.
See [distribution obligations](THIRD_PARTY.md) before bundling libraries.

The mpv backend keeps one core during normal playback. The wakeup callback only
posts a coalesced Qt invocation; the owning thread drains bounded batches.
Commands are asynchronous and controls coalesce while replies are outstanding.
Replacement retires the previous native load before starting the latest pending
source; observer generations, command IDs, and playlist entry IDs reject stale
notifications. Deferred seeks preserve pause intent. Stop retains the source
for explicit restart; cancel invalidates notifications and releases media.
Blocking final native destruction runs through Qt's thread pool after callbacks
are detached. No permanent engine worker or polling timer is introduced.
Selection happens when MusicService is constructed; restart the application to
change the environment setting. Direct HTTPS media uses explicit TLS certificate verification.

Video tracks, cover art, subtitles, windows, user mpv configuration/scripts, and
mpv's extraction hook are disabled. A source with video and audio still plays
its audio; yt-dlp/Deno and the validated header-aware loopback relay remain the
YouTube path. mpv's normal audio/buffering defaults are retained.

### Production timing

```sh
VELUNE_PLAYBACK_TRACE=1 VELUNE_PLAYBACK_BACKEND=mpv desktop/build/velune
```

Timing is disabled by default. Enabled records use a monotonic clock and contain
request ID, backend/source category, and milliseconds for the play request,
resolver start/completion/cache hit, relay listening, backend load, parsed media,
active playback, and first positive position proxy. Refresh preparation has
separate markers. Local media bypasses resolver/relay stages. Cancellation,
completion and errors finalize the bounded current record. URLs, paths, track
IDs, cookies, headers and helper diagnostics are not recorded.

**Playing, parsed media, and position updates do not establish audible output.**
Production `first_audio_output_ms` stays null. In particular, seeking can cause
position changes. The optional benchmark capture reports nonzero PCM received
from an isolated virtual sink, including capture buffering; it does not verify
physical speakers or human audibility.

### Reproduce comparative benchmarks

FFmpeg generates synthetic fixtures only; it is not a new application runtime.
The benchmark exercises both engines with files and the production `AudioStream`
HTTP relay, whose injected test transport implements deterministic range replies.
An optional native resolver fixture measures helper startup separately. No CDN,
account or external service is contacted in this mode.

```sh
cmake -DOUTPUT_DIR="$PWD/desktop/build/codec-fixtures" \
  -P desktop/benchmarks/generate-fixtures.cmake

# Requires test-only pulseaudio/pulseaudio-utils. No desktop routing is changed.
desktop/benchmarks/with-null-audio.sh cmake \
  -DBENCHMARK="$PWD/desktop/build/velune_playback_benchmark" \
  -DFIXTURE_DIR="$PWD/desktop/build/codec-fixtures" \
  -DOUTPUT_DIR="$PWD/desktop/build/playback-results" \
  -DRESOLVER_FIXTURE="$PWD/desktop/build/velune_resolver_fixture" \
  -DAUDIO_DEVICE=velune_benchmark -DITERATIONS=30 \
  -P desktop/benchmarks/run-matrix.cmake

cmake -DINPUT_DIR="$PWD/desktop/build/playback-results" \
  -DOUTPUT="$PWD/desktop/build/playback-summary.csv" \
  -P desktop/benchmarks/summarize-results.cmake
```

The matrix covers MP3, M4A/AAC, Opus, FLAC, WAV, and an audio/video MP4, with cold,
warm, switching and seeking scenarios. Cold launches a fresh process; OS caches
are retained. Raw samples, counts, medians, nearest-rank p95, errors, buffering
progress/recovery, and separate resolver/backend timings are saved as JSON.
A failed backend cannot silently turn into the other backend in a comparison.

Additional examples (substitute `--backend qt` to compare):

```sh
# Capture only from this explicitly isolated synthetic-audio sink.
desktop/benchmarks/with-null-audio.sh desktop/build/velune_playback_benchmark \
  --backend mpv --audio-device velune_benchmark \
  --fixture desktop/build/codec-fixtures/tone.wav --scenario warm --iterations 30 \
  --pulse-monitor velune_benchmark.monitor --output desktop/build/mpv-output.json

# Idle/active WAV CPU and RSS: nominal 30-second windows, one-second samples.
desktop/benchmarks/with-null-audio.sh desktop/build/velune_playback_benchmark \
  --backend mpv --audio-device velune_benchmark \
  --fixture desktop/build/codec-fixtures/tone.wav --scenario resources \
  --resource-seconds 30 --output desktop/build/mpv-resources.json

# Repeated loading, pause/resume spam, paused seek, stop and explicit restart.
desktop/benchmarks/with-null-audio.sh desktop/build/velune_playback_benchmark \
  --backend mpv --audio-device velune_benchmark \
  --fixture desktop/build/codec-fixtures/tone.wav --scenario stability --iterations 100 \
  --output desktop/build/mpv-stability.json
```

A controlled midstream outage uses `--scenario buffering --transport relay`
and requires PCM capture and a WAV of at least 10 seconds. For example, generate
an isolated 20-second synthetic tone and run three trials:

```sh
ffmpeg -nostdin -f lavfi -i 'sine=frequency=997:sample_rate=48000:duration=20' \
  -ac 1 -c:a pcm_s16le desktop/build/codec-fixtures/buffering.wav
desktop/benchmarks/with-null-audio.sh desktop/build/velune_playback_benchmark \
  --backend mpv --audio-device velune_benchmark \
  --fixture desktop/build/codec-fixtures/buffering.wav --scenario buffering \
  --transport relay --iterations 3 --pulse-monitor velune_benchmark.monitor \
  --output desktop/build/mpv-buffering.json
```

The injected upstream releases 16 KiB every 50 ms and pauses for 5 seconds after
initial playback/output. `pcm_gap_ms` measures a nonzero-PCM gap of at least
300 ms through the next nonzero sample; backend prefill can make this shorter
than the upstream pause. Both backends recovered 3/3 times in the saved run;
Qt/mpv median gaps were 2590.1/3104.9 ms. This virtual-sink measurement includes
capture buffering and does not establish physical audibility.

Use `--upstream-delay-ms 250 --transport relay` to exercise delayed upstream
responses. Output sample capture supports warm/once and buffering scenarios: identical
source tones cannot identify a switch boundary while the previous track plays.
Backend state/position metrics also have different native notification cadences;
do not interpret their differences as direct first-audio comparisons.

### Automated checks and limitations

`mpv-playback` tests initialization failure, deterministic stale native events,
queue overflow, real decoding with null output, deferred/paused seeks, control
spam, rapid switching, EOF/restart, relay seeking, buffering stop/destruction,
and native MusicService Connect routing/logout. Native cases explicitly skip
when headers/runtime are absent; factory/trace tests still run. Set
`VELUNE_CODEC_FIXTURES` to the generated directory to include every format in
CTest. FakePlaybackEngine lifecycle tests, authentication, resolver, relay, UI,
search and TLS Connect tests are retained. The native Qt UI test skips when no
output device exists; Linux CI supplies an isolated null sink.

The desktop CI workflow adds Linux Release/Debug, mpv-disabled and minimal jobs,
and Windows MSVC compilation/deployment checks with optional headers enabled and
disabled. Windows native mpv runtime validation requires an audited DLL and has
not been performed locally. Live YouTube remains explicitly opt-in through the
existing `VELUNE_LIVE_PLAYBACK_ID` test; it is not part of deterministic results.
Physical Android/TV interoperability and physical speaker output remain manual
checks. See [the measured results and validation status](benchmarks/RESULTS.md)
for the current recommendation and all untested conditions.

A public live-service benchmark is separately opt-in. Put the existing verified
helpers in `tools/` beside the benchmark or on PATH, then run, for example:

```sh
desktop/build/velune_playback_benchmark --backend mpv \
  --live-youtube-id PUBLIC_VIDEO_ID --allow-live --scenario warm --iterations 30 \
  --output desktop/build/live-mpv.json
```

Both explicit flags are required. This uses the existing public resolver/relay,
sends no account cookies, and saves no credentials or source URLs. Resolver cache
hits are retained in warm runs; resolver duration is reported separately. Public
service variability prevents treating this as a deterministic backend comparison.
No live benchmark was run without explicit live configuration in this session.
