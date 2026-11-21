# تسجيل Google في Velune

## أثناء استخدام التطبيق

1. اضغط **Continue with Google**. يفتح Velune المتصفح الافتراضي للجهاز، مثل Firefox أو Chrome.
2. اختر حساب Google ووافق على أن يقرأ Velune هويتك وقائمة YouTube المعجب بها.
3. ترجع النتيجة تلقائيًا إلى Velune. يتحقق التطبيق من Google، يفتح الواجهة، ويبدأ استيراد القائمة. لا يوجد نسخ كوكيز أو كود يدوي.

الرجوع يستعمل عنوان `127.0.0.1` خاصًا بنفس الجهاز. لا يُفتح منفذ على الشبكة، ولا تنتقل بيانات الحساب عبر Velune Connect. يتم حفظ الجلسة في مخزن كلمات مرور النظام إذا كان متاحًا، مع تجديدها تلقائيًا. زر Cancel sign-in يلغي الطلب الجاري. إغلاق المتصفح دون استكمال الدخول لا يسجّل الدخول إلى التطبيق، وتنتهي مهلة الطلب بعد 3 دقائق.

## إعداد مطلوب مرة واحدة لصاحب التطبيق

هذا مطلوب قبل أن يستطيع أي مستخدم تسجيل الدخول. Google يجب أن يعرف أن التطبيق اسمه Velune ولمن تعود ملكيته. لا يُطلب من مستخدمي النسخة المنشورة إنشاء مشروع خاص بهم؛ تُضمّن بيانات تسجيل تطبيق Velune في التوزيعة بعد إعدادها.

1. افتح [Google Cloud Console](https://console.cloud.google.com/) بحسابك، وأنشئ أو اختر مشروعًا لـ**Velune**.
2. افتح [YouTube Data API v3](https://console.cloud.google.com/apis/library/youtube.googleapis.com) داخل المشروع واضغط **Enable**.
3. افتح [Google Auth Platform](https://console.cloud.google.com/auth/overview). في Branding عيّن اسم التطبيق **Velune** وبريد تواصل تملكه. في Audience اختر External / Testing وأضف حسابك إلى Test users أثناء التطوير.
4. في Data Access أضف `openid` و`email` و`profile` و`https://www.googleapis.com/auth/youtube.readonly`. النطاق الأخير للقراءة فقط، ولا يسمح للتطبيق بتعديل حساب YouTube.
5. في Clients أنشئ OAuth client من نوع **Desktop app** باسم **Velune Desktop**، ثم نزّل ملف JSON. لا تختَر Web application أو Android. Google قد يطلب إتمام إعدادات المشروع أو قبول شروطه بواسطة صاحب الحساب.
6. ركّب الملف محليًا من مجلد المشروع:

```sh
desktop/scripts/configure-google-login.sh "/path/to/downloaded-client.json"
```

السكربت يتحقق من أن الملف Desktop client وينسخه إلى `~/.config/Velune/Velune/google-oauth.json` بصلاحيات صاحب الحساب فقط. لا يطبع محتواه. أعد فتح Velune واضغط Continue with Google.

يمكن أثناء التطوير تحديد الملف بـ`VELUNE_GOOGLE_OAUTH_CONFIG=/absolute/path.json`، أو تحديد `VELUNE_GOOGLE_CLIENT_ID` و`VELUNE_GOOGLE_CLIENT_SECRET` الاختياري. يمكن وضع `google-oauth.json` في مجلد بيانات المكتبة لتخصيص نسخة اختبار منفصلة. لا تُضاف إعدادات الحساب الحقيقية إلى Git. لتجهيز توزيعة للمستخدمين، يضع صاحب التطبيق ملف التسجيل في `desktop/config/google-oauth.json` محليًا؛ سكربت تثبيت Linux يركّبه تلقائيًا ولا يحتاج المستخدم النهائي إنشاء مشروع Google Cloud.

في النسخ المنشورة لعموم المستخدمين قد تتطلب أذونات Google مراجعة التطبيق؛ وضع Testing مخصص للحسابات المدرجة للاختبار. لا يوجد OAuth client خاص بـVelune مسجل أو مهيأ في هذا المشروع حاليًا، لذلك الاختبارات المحلية وحدها لا تعني أن الدخول الحقيقي أصبح متاحًا.

## ما الذي يستورده تسجيل الدخول؟

تسجيل المتصفح يمنح التطبيق OAuth tokens للـAPIs المصرّح بها. لا يمنح التطبيق كوكيز موقع YouTube Music في متصفحك. يستخدم Velune التفويض لجلب قائمة YouTube المعجب بها عبر API رسمي، حتى أحدث 1000 عنصر حسب حد Google. هذه القائمة قد تشمل فيديوهات، ولا يتيح هذا المسار الرسمي نسخ كل مجموعات YouTube Music والألبومات والفنانين كما تظهر في الموقع. البحث العام والراديو ومسار تشغيل الأغاني يظلون منفصلين عن جلسة OAuth.

مراجع التنفيذ: [Google OAuth for desktop apps](https://developers.google.com/identity/protocols/oauth2/native-app)، [YouTube videos.list](https://developers.google.com/youtube/data/reference/rest/v3/videos/list).
