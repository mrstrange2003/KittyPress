package com.deepion.kittypress

object KittyPressNative {
    init {
        System.loadLibrary("kittypress") // match your native .so name
    }

    // returns 0 on success, non-zero on error (matches your native implementation)
    external fun compressNative(inputArray: Array<String>, outPath: String): Int
    external fun decompressNative(archive: String, outDir: String): String?
}
