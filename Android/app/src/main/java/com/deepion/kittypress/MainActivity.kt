package com.deepion.kittypress

import androidx.appcompat.app.AppCompatDelegate
import androidx.appcompat.widget.Toolbar
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.util.Log
import android.view.Menu
import android.view.MenuItem
import android.widget.Button
import android.widget.TextView
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.documentfile.provider.DocumentFile
import kotlinx.coroutines.*
import java.io.File

class MainActivity : AppCompatActivity() {

    private val TAG = "KittyPress"

    private lateinit var statusTv: TextView

    private val selectedFileUris = mutableListOf<Uri>()
    private val selectedFolderUris = mutableListOf<Uri>()
    private val selectedInputsOrdered = mutableListOf<Uri>()

    private var pendingArchiveToCopy: File? = null
    private var pendingArchiveName: String? = null

    companion object {
        const val IntentFlagsForTree =
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION

        const val PREFS_NAME = "kittypress_prefs"
        const val PREF_KEY_THEME = "theme_mode"
    }

    // ============================================================
    // Menu (Dark/Light Mode toggle + Exit)
    // ============================================================
    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        menuInflater.inflate(R.menu.main_menu, menu)

        // Update menu text depending on saved/current mode
        val current = AppCompatDelegate.getDefaultNightMode()
        val item = menu.findItem(R.id.action_toggle_theme)
        item?.title = if (current == AppCompatDelegate.MODE_NIGHT_YES)
            "Switch to Light Mode"
        else
            "Switch to Dark Mode"

        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            R.id.action_toggle_theme -> {
                // Persist choice
                val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                val editor = prefs.edit()

                val current = AppCompatDelegate.getDefaultNightMode()
                val newMode =
                    if (current == AppCompatDelegate.MODE_NIGHT_YES)
                        AppCompatDelegate.MODE_NIGHT_NO
                    else
                        AppCompatDelegate.MODE_NIGHT_YES

                editor.putInt(PREF_KEY_THEME, newMode)
                editor.apply()

                AppCompatDelegate.setDefaultNightMode(newMode)

                // Recreate activity to apply theme permanently
                recreate()
                return true
            }

            R.id.action_exit -> {
                finishAffinity()
                return true
            }
        }
        return super.onOptionsItemSelected(item)
    }

    // ============================================================
    // Pickers
    // ============================================================
    private val pickFilesLauncher =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (!uris.isNullOrEmpty()) {
                selectedFileUris.clear()
                selectedFileUris.addAll(uris)

                // update ordered list: remove any previous file entries then append new picks
                selectedInputsOrdered.removeAll { it in selectedFileUris }
                for (u in uris) {
                    selectedInputsOrdered.remove(u)
                    selectedInputsOrdered.add(u)
                }

                statusTv.text = "KittyPress status: Selected ${uris.size} file(s)"
            }
        }

    private val pickFolderLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { treeUri ->
            if (treeUri != null) {
                try {
                    contentResolver.takePersistableUriPermission(treeUri, IntentFlagsForTree)
                } catch (_: Exception) { }

                selectedFolderUris.add(treeUri)
                selectedInputsOrdered.remove(treeUri)
                selectedInputsOrdered.add(treeUri)

                statusTv.text = "KittyPress status: Selected ${selectedFolderUris.size} folder(s)"

                val pending = pendingArchiveToCopy
                val name = pendingArchiveName
                if (pending != null && name != null) {
                    CoroutineScope(Dispatchers.IO).launch {
                        val ok = copyFileToDocumentFolder(pending, treeUri, name)
                        withContext(Dispatchers.Main) {
                            statusTv.text = if (ok) "Saved → $name" else "Failed to save → $name"
                        }
                        pendingArchiveToCopy = null
                        pendingArchiveName = null
                    }
                }
            }
        }

    private val pickArchiveLauncher =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
            if (uri != null) handleDecompressUri(uri)
        }

    // ============================================================
    // onCreate
    // ============================================================
    override fun onCreate(savedInstanceState: Bundle?) {
        // Load permanent theme before UI inflates
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val savedMode = prefs.getInt(PREF_KEY_THEME, AppCompatDelegate.MODE_NIGHT_NO)
        AppCompatDelegate.setDefaultNightMode(savedMode)

        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val toolbar = findViewById<Toolbar>(R.id.mainToolbar)
        setSupportActionBar(toolbar)

        // ActionBar Logo + Title
        supportActionBar?.setDisplayShowHomeEnabled(true)
        supportActionBar?.setLogo(R.mipmap.ic_launcher)
        supportActionBar?.setDisplayUseLogoEnabled(true)
        supportActionBar?.title = " KittyPress"

        statusTv = findViewById(R.id.tv_status)

        findViewById<Button>(R.id.btn_pick_files).setOnClickListener {
            pickFilesLauncher.launch(arrayOf("*/*"))
        }
        findViewById<Button>(R.id.btn_pick_folder).setOnClickListener {
            pickFolderLauncher.launch(null)
        }

        findViewById<Button>(R.id.btn_compress).setOnClickListener {
            statusTv.text = "KittyPress status: Preparing to compress..."
            CoroutineScope(Dispatchers.Main).launch { compressAction() }
        }

        findViewById<Button>(R.id.btn_decompress).setOnClickListener {
            pickArchiveLauncher.launch(arrayOf("*/*"))
        }
    }

    // ============================================================
    // COMPRESSION
    // ============================================================
    private suspend fun compressAction() {
        withContext(Dispatchers.IO) {
            try {
                val inputsRoot = File(cacheDir, "inputs_${System.currentTimeMillis()}")
                inputsRoot.mkdirs()

                val inputsToPass = mutableListOf<String>()

                // Copy folders
                for (treeUri in selectedFolderUris) {
                    val doc = DocumentFile.fromTreeUri(this@MainActivity, treeUri) ?: continue
                    val folderName = sanitizeFilename(doc.name ?: "folder")
                    val dest = File(inputsRoot, folderName)
                    dest.mkdirs()
                    copyDocumentTree(doc, dest)
                    inputsToPass.add(dest.absolutePath)
                }

                // Copy files
                for (fileUri in selectedFileUris) {
                    val name = sanitizeFilename(uriDisplayName(fileUri) ?: "file")
                    val out = File(inputsRoot, name)
                    copyUriToFile(fileUri, out)
                    inputsToPass.add(out.absolutePath)
                }

                if (inputsToPass.isEmpty()) {
                    withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: No input selected." }
                    return@withContext
                }

                val first = selectedInputsOrdered.firstOrNull()
                val baseName =
                    computeBaseNameForArchive(first) ?: "archive_${System.currentTimeMillis()}"
                val outName = "$baseName.kitty"
                val outCache = File(cacheDir, outName)

                val rc = KittyPressNative.compressNative(
                    inputsToPass.toTypedArray(),
                    outCache.absolutePath
                )

                if (rc != 0) {
                    withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Compression failed." }
                    return@withContext
                }

                val firstResolved: Uri? =
                    first ?: selectedFileUris.firstOrNull() ?: selectedFolderUris.firstOrNull()

                val parent: Uri? =
                    if (firstResolved != null) tryGetParentFolderUri(firstResolved)
                    else null

                if (parent != null) {
                    try {
                        val folder = DocumentFile.fromTreeUri(this@MainActivity, parent)
                        val created = folder?.createFile("application/octet-stream", outName)
                        if (created != null) {
                            contentResolver.openOutputStream(created.uri)?.use { out ->
                                outCache.inputStream().copyTo(out)
                            }
                            withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Archive saved: $outName" }
                        } else throw SecurityException()

                    } catch (_: Exception) {
                        pendingArchiveToCopy = outCache
                        pendingArchiveName = outName
                        withContext(Dispatchers.Main) {
                            pickFolderLauncher.launch(parent)
                            statusTv.text = "KittyPress status: Grant access to save archive"
                        }
                    }
                } else {
                    pendingArchiveToCopy = outCache
                    pendingArchiveName = outName
                    withContext(Dispatchers.Main) {
                        pickFolderLauncher.launch(null)
                        statusTv.text = "KittyPress status: Choose folder to save archive"
                    }
                }

            } catch (ex: Exception) {
                Log.e(TAG, "compressAction", ex)
                withContext(Dispatchers.Main) {
                    statusTv.text = "KittyPress status: Error: ${ex.message}"
                }
            }
        }
    }

    // ============================================================
    // DECOMPRESSION
    // ============================================================
    private fun handleDecompressUri(uri: Uri) {
        CoroutineScope(Dispatchers.Main).launch {
            withContext(Dispatchers.IO) {
                try {
                    val name = uriDisplayName(uri) ?: "archive.kitty"
                    val cacheArchive = File(cacheDir, "in_${System.currentTimeMillis()}_$name")
                    copyUriToFile(uri, cacheArchive)

                    val outDir = File(cacheDir, "out_${System.currentTimeMillis()}")
                    outDir.mkdirs()

                    statusTv.text = "KittyPress status: Running native decompress..."

                    val extractedRootName = KittyPressNative.decompressNative(
                        cacheArchive.absolutePath,
                        outDir.absolutePath
                    )

                    if (extractedRootName == null) {
                        withContext(Dispatchers.Main) {
                            statusTv.text = "KittyPress status: Decompress failed."
                        }
                        return@withContext
                    }

                    val extractedRoot = File(outDir, extractedRootName)
                    if (!extractedRoot.exists()) {
                        withContext(Dispatchers.Main) {
                            statusTv.text = "KittyPress status: Extraction incomplete."
                        }
                        return@withContext
                    }

                    val parentTree = tryGetParentFolderUri(uri)
                    if (parentTree != null) {
                        try {
                            copyDirToDocumentFolder(extractedRoot, parentTree)
                            withContext(Dispatchers.Main) {
                                statusTv.text = "KittyPress status: Saved to folder."
                            }
                        } catch (_: SecurityException) {
                            withContext(Dispatchers.Main) {
                                statusTv.text = "KittyPress status: Grant permission to save files"
                                pickFolderLauncher.launch(parentTree)
                            }
                        }
                    } else {
                        withContext(Dispatchers.Main) {
                            statusTv.text = "KittyPress status: Pick folder to save extracted files"
                            pickFolderLauncher.launch(null)
                        }
                    }

                } catch (ex: Exception) {
                    Log.e(TAG, "handleDecompressUri", ex)
                    withContext(Dispatchers.Main) {
                        statusTv.text = "KittyPress status: Error: ${ex.message}"
                    }
                }
            }
        }
    }

    // ============================================================
    // Copy extracted root folder/file
    // ============================================================
    private fun copyDirToDocumentFolder(src: File, folderTreeUri: Uri) {
        val root = DocumentFile.fromTreeUri(this, folderTreeUri) ?: return

        if (src.isFile) {
            val name = src.name
            root.findFile(name)?.delete()
            val created = root.createFile("application/octet-stream", name)
            contentResolver.openOutputStream(created!!.uri)?.use { out ->
                src.inputStream().copyTo(out)
            }
            return
        }

        fun recurse(curr: File, rel: String) {
            curr.listFiles()?.forEach { f ->
                if (f.isDirectory) {
                    val sub = if (rel.isEmpty()) f.name else "$rel/${f.name}"
                    var d: DocumentFile? = root
                    for (seg in sub.split("/"))
                        d = d?.findFile(seg) ?: d?.createDirectory(seg)
                    recurse(f, sub)
                } else {
                    val relPath = if (rel.isEmpty()) f.name else "$rel/${f.name}"
                    val parts = relPath.split("/").toMutableList()
                    val filename = parts.removeLast()
                    var d: DocumentFile? = root
                    for (seg in parts)
                        d = d?.findFile(seg) ?: d?.createDirectory(seg)

                    d?.findFile(filename)?.delete()
                    val created = d?.createFile("application/octet-stream", filename)
                    contentResolver.openOutputStream(created!!.uri)?.use { out ->
                        f.inputStream().copyTo(out)
                    }
                }
            }
        }

        recurse(src, "")
    }

    // ============================================================
    // Helpers
    // ============================================================
    private fun sanitizeFilename(n: String) =
        n.replace(Regex("[\\\\/:*?\"<>|]"), "_").take(120)

    private fun uriDisplayName(uri: Uri): String? {
        try {
            DocumentFile.fromSingleUri(this, uri)?.name?.let { return it }
        } catch (_: Exception) {}

        try {
            contentResolver.query(
                uri,
                arrayOf(android.provider.OpenableColumns.DISPLAY_NAME),
                null, null, null
            )?.use { cur ->
                if (cur.moveToFirst()) {
                    val idx = cur.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
                    if (idx >= 0) return cur.getString(idx)
                }
            }
        } catch (_: Exception) {}

        return uri.lastPathSegment
    }

    private fun copyUriToFile(uri: Uri, out: File) {
        contentResolver.openInputStream(uri)?.use { input ->
            out.outputStream().use { output -> input.copyTo(output) }
        }
    }

    private fun copyDocumentTree(doc: DocumentFile, dest: File) {
        if (doc.isDirectory) {
            dest.mkdirs()
            doc.listFiles().forEach { child ->
                if (child.isDirectory) {
                    val sub = File(dest, sanitizeFilename(child.name ?: "dir"))
                    copyDocumentTree(child, sub)
                } else {
                    val outFile = File(dest, sanitizeFilename(child.name ?: "file"))
                    contentResolver.openInputStream(child.uri)?.use { input ->
                        outFile.outputStream().use { output -> input.copyTo(output) }
                    }
                }
            }
        } else {
            val outFile = File(dest, sanitizeFilename(doc.name ?: "file"))
            contentResolver.openInputStream(doc.uri)?.use { input ->
                outFile.outputStream().use { output -> input.copyTo(output) }
            }
        }
    }

    private fun tryGetParentFolderUri(fileUri: Uri): Uri? {
        return try {
            val docId = DocumentsContract.getDocumentId(fileUri)
            if (docId.startsWith("primary:")) {
                val parts = docId.split(":")
                if (parts.size == 2) {
                    val rel = parts[1]
                    val segs = rel.split("/").toMutableList()
                    if (segs.isNotEmpty()) {
                        segs.removeLast()
                        val path = segs.joinToString("/")
                        val treeId = if (path.isEmpty()) "primary:" else "primary:$path"
                        return DocumentsContract.buildTreeDocumentUri(
                            "com.android.externalstorage.documents",
                            treeId
                        )
                    }
                }
            }
            DocumentFile.fromSingleUri(this, fileUri)?.parentFile?.uri
        } catch (_: Exception) {
            null
        }
    }

    private fun computeBaseNameForArchive(uri: Uri?): String? {
        uri ?: return null
        try {
            val d = DocumentFile.fromSingleUri(this, uri)
            if (d?.name != null) {
                if (d.isDirectory) return d.name!!
                return d.name!!.substringBeforeLast('.', d.name!!)
            }
        } catch (_: Exception) {}

        try {
            val t = DocumentFile.fromTreeUri(this, uri)
            if (t?.name != null) return t.name!!
        } catch (_: Exception) {}

        return uriDisplayName(uri)?.substringBeforeLast('.', uriDisplayName(uri)!!)
    }

    private fun copyFileToDocumentFolder(src: File, folder: Uri, name: String): Boolean {
        return try {
            val root = DocumentFile.fromTreeUri(this, folder) ?: return false
            root.findFile(name)?.delete()
            val created = root.createFile("application/octet-stream", name) ?: return false
            contentResolver.openOutputStream(created.uri)?.use { out ->
                src.inputStream().use { it.copyTo(out) }
            }
            true
        } catch (_: Exception) {
            false
        }
    }
}
