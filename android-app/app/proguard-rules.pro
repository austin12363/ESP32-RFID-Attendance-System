# Keep Retrofit data models
-keepclassmembers class * {
    @com.google.gson.annotations.SerializedName <fields>;
}
-keep class com.slsu.attendance.data.model.** { *; }
