package com.izzy2lost.x1box

import android.content.Context

/** Installed package identity, shared by Settings and About. */
object BuildIdentity {
  fun display(context: Context): String {
    val variant = when (BuildConfig.BUILD_TYPE) {
      "debug" -> R.string.build_variant_debug
      "profile" -> R.string.build_variant_profile
      else -> R.string.build_variant_release
    }
    return context.getString(
      R.string.build_version_format,
      context.getString(R.string.app_name),
      BuildConfig.VERSION_NAME,
      context.getString(variant),
      BuildConfig.VERSION_CODE,
    )
  }
}
