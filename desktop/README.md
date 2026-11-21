# Velune Desktop

واجهة Windows/Linux مستقلة بلغة **C++20 / Qt 6 Widgets**. تطبيق Android الحالي يظل مستقلًا.

التصميم يتبع الصورة المرجعية: AMOLED أسود، قائمة جانبية ثابتة، شبكة 3×3 بالترتيب المحدد، ثلاثة كروت، ومشغل ثابت بارتفاع 128px. خانة **Liked** ثابتة أولًا وبدون قلب. شريط التقديم في منتصف المشغل تحت أزرار التشغيل. الأيقونات والموجات مرسومة داخل Qt بدون صور زخرفية خارجية. ألوان الواجهة مجمعة في `src/ui/Theme.h`.

## التشغيل والبناء

المتطلبات: CMake 3.21+، مترجم C++20، OpenSSL، Qt 6.4+ مع Widgets / Network / Multimedia / Test، ومكتبة Qt6Keychain لحفظ الجلسة في مخزن النظام.

Ubuntu/Debian:

```sh
sudo apt install build-essential cmake qt6-base-dev qt6-multimedia-dev qtkeychain-qt6-dev libssl-dev fonts-inter
cmake -S desktop -B desktop/build -DCMAKE_BUILD_TYPE=Release
cmake --build desktop/build -j4
ctest --test-dir desktop/build --output-on-failure
desktop/build/velune
```

Windows: استخدم Qt MSVC أو MinGW المطابق للمترجم، واجعل Qt وOpenSSL متاحين في `CMAKE_PREFIX_PATH`، ثم نفس أوامر CMake. استخدم `windeployqt` لتجهيز DLLs وضمّن مكتبة Qt6Keychain عند التغليف. لم يتم بناء أو تشغيل نسخة Windows في هذه الجلسة.

يمكن تمرير `-DVELUNE_MINIMAL_BUILD=ON` للعمل دون الصوت المحلي أو حفظ الجلسة إذا لم تتوفر اعتماداتهما. تسجيل الدخول نفسه يعتمد على متصفح النظام ولا يحتاج Qt WebEngine. النسخة المثبتة مبنية بالصوت وQt6Keychain.

## التثبيت لحساب Linux الحالي

```sh
desktop/scripts/install-linux.sh desktop/build
```

التثبيت في `~/.local/lib/velune`، وأمر التشغيل `~/.local/bin/velune`. تظهر قائمتان في التطبيقات:

- **Velune**: يبدأ بشاشة مموّهة ويتطلب جلسة Google صحيحة.
- **Velune — UI Preview**: يعرض التصميم مباشرة في ملف بيانات منفصل، دون إنشاء تسجيل دخول أو بيانات موسيقى وهمية. الأزرار تستخدم الخدمات الفعلية، ويمكن استيراد ملفات صوت من Settings.

يقبل سكربت التثبيت مسارًا ثانيًا اختياريًا لجذر حزم Qt المستخرجة، لتجهيز المكتبات محليًا دون صلاحيات administrator. لا يغيّر مكتبات النظام ولا يعطل TLS أو حماية المتصفح.

حذف التثبيت: احذف `~/.local/lib/velune` و`~/.local/bin/velune` ومدخلي `velune*.desktop` من `~/.local/share/applications`. بيانات المكتبة منفصلة ولا يحذفها المثبّت.

## الوظائف الموجودة

- التنقل والبحث وExplore والمكتبة وQueue، مفضلة محلية، shuffle/repeat، تقديم/تأخير، مستوى الصوت وmute، fullscreen، وأزرار النافذة مع السحب وتغيير الحجم.
- التركيز بلوحة المفاتيح، `Space` للتشغيل والإيقاف، `Ctrl+F` للبحث، و`F11` لملء الشاشة. السلايدر يدعم السحب والأسهم.
- تسجيل Google في **المتصفح الافتراضي للنظام** باستخدام OAuth Authorization Code + PKCE و`prompt=select_account consent`. يستقبل التطبيق الرد على `127.0.0.1` في منفذ مؤقت، ويفحص state وهوية الحساب قبل فتح الواجهة تلقائيًا. لا يقرأ متصفح المستخدم أو كوكيزه ولا يشغّل متصفحًا مضمّنًا.
- يحفظ refresh token في مخزن كلمات المرور للنظام عبر Qt6Keychain، ويجدد الجلسة في الخلفية. عند عدم توفر مخزن آمن تظل الجلسة في الذاكرة فقط، بدون fallback إلى ملف نصي. Sign out يمسح جلسة Velune المحلية ولا يسجّل الخروج من Google في المتصفح.
- استيراد الـLiked عبر YouTube Data API v3 بالتفويض الرسمي. هذه القائمة تشمل عناصر YouTube المعجب بها وليست نسخة مطابقة لكل مجموعات YouTube Music؛ Google يحد هذه القائمة بأحدث 1000 عنصر. [دليل إعداد Google مرة واحدة لصاحب التطبيق](GOOGLE_SIGN_IN.md).
- البحث وExplore وSong Radio يستخدمون طلبات الخدمة الفعلية، مع إظهار الأخطاء. الشبكة تعتمد على نفس WEB_REMIX وإعداداته الحالية في المشروع.
- استيراد وتشغيل صوت محلي باستخدام Qt Multimedia. تشغيل YouTube المباشر متاح عندما ترجع الخدمة audio URL صالحًا؛ التواقيع المشفرة وPoToken وحالات التحقق الإضافية لم تُنقل بعد، ويظهر خطأ واضح عند عدم توفر المصدر.
- حفظ المكتبة atomically، واستيراد جماعي، ودمج likes/playlists/history بإصدارات Lamport وحفظ tombstones للتعامل مع الحذف والتعارض.

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

هذه بداية تطبيق Desktop وليست نقلًا كاملًا لكل مزايا Android. المتبقي: منسق Connect/إعلان Desktop كجهاز renderer، انتقال الملكية prepare/release/commit إلى الكمبيوتر، media consumer وتحميل cache من الهاتف، assets/lyrics، استيراد Google playlists/albums/artists بالكامل، تنزيلات الخلفية، تحديثات التطبيق وتكاملات Android الأخرى. طلب نقل ملكية الصوت إلى الكمبيوتر يفشل صراحةً؛ لا يرد التطبيق READY قبل وجود renderer قادر على التشغيل.

إصدار 0.1.1 يلغي مسار تسجيل WebEngine السابق. اختبارات OAuth تستخدم خادمًا محليًا يمثل ردود Google للتحقق من الرجوع والتفويض والإلغاء وPKCE والتجديد؛ ليست تجربة حساب Google حقيقي. لا توجد بيانات تسجيل Google OAuth خاصة بـVelune في المشروع حاليًا، ولذلك يبقى الدخول الحقيقي متوقفًا على إنشاء Desktop OAuth client من حساب صاحب المشروع وتفعيل YouTube Data API v3. لا يفتح التطبيق الحساب بمجرد فتح المتصفح أو وصول code قبل التحقق.

## التحقق

اختبارات Qt تغطي: تجزئة الإطارات وحد الحجم ورفض إعادة التسلسل، عناوين LAN، حذف/تعارض المكتبة وثبات الهوية عبر التشغيل، **TLS loopback حقيقي** ورسائل الهوية والجلسة وأوامر ACK وقراءة media range/EOF، وحالة تسجيل الخروج وشبكة 3×3 وثلاثة كروت ومكان شريط التقديم عند 1660×938 و1280×714 و1000×680.

تغطي اختبارات Google أيضًا فتح المتصفح الافتراضي عبر Qt، اختيار الحساب، رفض state غير المطابق، code exchange، هوية الحساب المؤكدة، رفض الأذونات الناقصة، رفض token، الإلغاء، منع إعادة استخدام الرد، وتجديد الجلسة. لا تُكتب بيانات اعتماد الاختبار إلى مخزن كلمات مرور المستخدم.

التقاط نسخة الواجهة للمراجعة دون حساب:

```sh
desktop/build/velune --preview --screenshot /tmp/velune-home.png --data-dir /tmp/velune-ui-review
```
