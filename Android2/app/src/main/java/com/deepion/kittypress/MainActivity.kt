package com.deepion.kittypress

import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.DocumentsContract
import android.util.Log
import android.widget.Button
import android.widget.TextView
import androidx.activity.result.ActivityResultLauncher
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.app.AppCompatDelegate
import androidx.appcompat.widget.Toolbar
import androidx.documentfile.provider.DocumentFile
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import kotlinx.coroutines.launch
import java.io.File
import java.io.IOException

class MainActivity : AppCompatActivity() {

    private val TAG = "KittyPress"

    private lateinit var statusTv: TextView
    private lateinit var btnCompress: Button
    private lateinit var btnPickFiles: Button
    private lateinit var btnPickFolder: Button
    private lateinit var btnPickArchive: Button
    private lateinit var btnExtractSelected: Button

    private var workingFolderTreeUri: Uri? = null

    private val selectedFileUris = mutableListOf<Uri>()
    private val selectedFolderUris = mutableListOf<Uri>()
    private val selectedInputsOrdered = mutableListOf<Uri>()

    private var pendingArchiveToCopy: File? = null
    private var pendingArchiveName: String? = null

    private var selectedArchiveUri: Uri? = null
    private var pendingArchiveToExtract: Uri? = null

    @Volatile private var isCompressing = false
    @Volatile private var isExtracting = false

    companion object {
        const val IntentFlagsForTree =
            Intent.FLAG_GRANT_READ_URI_PERMISSION or Intent.FLAG_GRANT_WRITE_URI_PERMISSION

        const val PREFS_NAME = "kittypress_prefs"
        const val PREF_KEY_THEME = "theme_mode"
    }

    private val pickFilesLauncher: ActivityResultLauncher<Array<String>> =
        registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            if (!uris.isNullOrEmpty()) {
                selectedFileUris.clear()
                selectedFileUris.addAll(uris)
                selectedInputsOrdered.removeAll { it in selectedFileUris }
                for (u in uris) {
                    selectedInputsOrdered.remove(u)
                    selectedInputsOrdered.add(u)
                }
                statusTv.text = "KittyPress status: Selected ${uris.size} file(s)"
            } else {
                statusTv.text = "KittyPress status: No files selected."
            }
        }

    private val pickFolderLauncher: ActivityResultLauncher<Uri?> =
        registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { treeUri ->
            if (treeUri == null) {
                statusTv.text = "KittyPress status: No folder picked."
                return@registerForActivityResult
            }
            try {
                contentResolver.takePersistableUriPermission(treeUri, IntentFlagsForTree)
            } catch (ex: Exception) {
                Log.w(TAG, "takePersistableUriPermission failed", ex)
            }

            val pendingArchive = pendingArchiveToCopy
            val pendingName = pendingArchiveName
            if (pendingArchive != null && pendingName != null) {
                lifecycleScope.launch(Dispatchers.IO) {
                    val ok = copyFileToDocumentFolder(pendingArchive, treeUri, pendingName)
                    pendingArchiveToCopy = null
                    pendingArchiveName = null
                    if (pendingArchive.exists()) pendingArchive.delete()
                    withContext(Dispatchers.Main) {
                        statusTv.text = if (ok) "KittyPress status: Archive saved: $pendingName"
                        else "KittyPress status: Failed to save archive."
                    }
                }
                return@registerForActivityResult
            }

            val pendingExtract = pendingArchiveToExtract
            if (pendingExtract != null) {
                pendingArchiveToExtract = null
                lifecycleScope.launch(Dispatchers.IO) {
                    try {
                        runExtractToFolderInternal(pendingExtract, treeUri)
                        withContext(Dispatchers.Main) {
                            statusTv.text = "KittyPress status: Extracted to chosen folder."
                        }
                    } catch (ex: Exception) {
                        Log.e(TAG, "Error extracting to picked folder", ex)
                        withContext(Dispatchers.Main) {
                            statusTv.text = "KittyPress status: Failed to extract: ${ex.message}"
                        }
                    }
                }
                return@registerForActivityResult
            }

            workingFolderTreeUri = treeUri
            selectedFolderUris.clear()
            selectedFolderUris.add(treeUri)
            selectedInputsOrdered.remove(treeUri)
            selectedInputsOrdered.add(treeUri)
            statusTv.text = "KittyPress status: Working folder selected: ${DocumentFile.fromTreeUri(this, treeUri)?.name ?: treeUri.lastPathSegment}"
        }

    private val pickArchiveLauncher: ActivityResultLauncher<Array<String>> =
        registerForActivityResult(ActivityResultContracts.OpenDocument()) { uri: Uri? ->
            if (uri != null) {
                selectedArchiveUri = uri
                val name = uriDisplayName(uri) ?: uri.lastPathSegment ?: "archive.kitty"
                statusTv.text = "KittyPress status: Selected archive: $name"
            } else {
                statusTv.text = "KittyPress status: No archive selected."
            }
        }

    override fun onCreate(savedInstanceState: Bundle?) {
        val prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        val savedMode = prefs.getInt(PREF_KEY_THEME, AppCompatDelegate.MODE_NIGHT_NO)
        AppCompatDelegate.setDefaultNightMode(savedMode)

        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        val toolbar = findViewById<Toolbar>(R.id.mainToolbar)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayShowHomeEnabled(true)
        supportActionBar?.setLogo(R.mipmap.ic_launcher)
        supportActionBar?.setDisplayUseLogoEnabled(true)
        supportActionBar?.title = " KittyPress"

        statusTv = findViewById(R.id.tv_status)
        btnCompress = findViewById(R.id.btn_compress)
        btnPickFiles = findViewById(R.id.btn_pick_files)
        btnPickFolder = findViewById(R.id.btn_pick_folder)
        btnPickArchive = findViewById(R.id.btn_pick_archive)
        btnExtractSelected = findViewById(R.id.btn_extract_selected)

        btnPickFiles.setOnClickListener { pickFilesLauncher.launch(arrayOf("*/*")) }
        btnPickFolder.setOnClickListener { pickFolderLauncher.launch(null) }

        btnCompress.setOnClickListener {
            if (isCompressing) {
                statusTv.text = "KittyPress status: Already compressing..."
                return@setOnClickListener
            }
            statusTv.text = "KittyPress status: Preparing to compress..."
            lifecycleScope.launch { compressAction() }
        }

        btnPickArchive.setOnClickListener {
            pickArchiveLauncher.launch(arrayOf("application/octet-stream", "*/*"))
        }

        btnExtractSelected.setOnClickListener {
            val archive = selectedArchiveUri
            if (archive == null) {
                statusTv.text = "KittyPress status: No archive selected. Use 'Choose KITTY File'."
                return@setOnClickListener
            }
            if (isExtracting) {
                statusTv.text = "KittyPress status: Extraction already in progress..."
                return@setOnClickListener
            }
            pendingArchiveToExtract = archive
            statusTv.text = "KittyPress status: Choose folder where extracted files will be placed"
            pickFolderLauncher.launch(null)
        }
    }

    private suspend fun compressAction() {
        withContext(Dispatchers.IO) {
            if (isCompressing) return@withContext
            isCompressing = true
            withContext(Dispatchers.Main) { btnCompress.isEnabled = false }

            try {
                val inputsRoot = File(cacheDir, "inputs_${System.currentTimeMillis()}")
                if (inputsRoot.exists()) inputsRoot.deleteRecursively()
                inputsRoot.mkdirs()

                val inputsToPass = mutableListOf<String>()

                for (treeUri in selectedFolderUris) {
                    val doc = DocumentFile.fromTreeUri(this@MainActivity, treeUri)
                    if (doc == null) {
                        withContext(Dispatchers.Main) { statusTv.text = "KittyPress: Cannot access selected folder." }
                        isCompressing = false
                        withContext(Dispatchers.Main) { btnCompress.isEnabled = true }
                        return@withContext
                    }

                    val folderName = doc.name ?: "folder"
                    val dest = File(inputsRoot, folderName)
                    dest.mkdirs()

                    // FIX: copy contents instead of duplicating the root folder
                    doc.listFiles().forEach { child ->
                        val childName = child.name ?: "unknown"
                        val childDest = File(dest, childName)

                        if (child.isDirectory) {
                            childDest.mkdirs()
                            copyDocumentTreeSafely(child, childDest)
                        } else {
                            contentResolver.openInputStream(child.uri)?.use { input ->
                                childDest.outputStream().use { out -> input.copyTo(out) }
                            }
                        }
                    }

                    inputsToPass.add(dest.absolutePath)
                }


                for (fileUri in selectedFileUris) {
                    val display = uriDisplayName(fileUri) ?: "file"
                    val out = File(inputsRoot, display)
                    copyUriToFile(fileUri, out)
                    inputsToPass.add(out.absolutePath)
                }

                if (inputsToPass.isEmpty()) {
                    withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: No input selected." }
                    isCompressing = false
                    withContext(Dispatchers.Main) { btnCompress.isEnabled = true }
                    return@withContext
                }

                val first = selectedInputsOrdered.firstOrNull()
                val baseName = computeBaseNameForArchive(first) ?: "archive_${System.currentTimeMillis()}"
                val outName = "$baseName.kitty"

                val outCache = File(cacheDir, outName)
                if (outCache.exists()) outCache.delete()

                withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Running native compress..." }

                val rc = KittyPressNative.compressNative(inputsToPass.toTypedArray(), outCache.absolutePath)
                if (rc != 0) {
                    withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Compression failed." }
                    if (outCache.exists()) outCache.delete()
                    inputsRoot.deleteRecursively()
                    isCompressing = false
                    withContext(Dispatchers.Main) { btnCompress.isEnabled = true }
                    return@withContext
                }

                pendingArchiveToCopy = outCache
                pendingArchiveName = outName
                withContext(Dispatchers.Main) {
                    statusTv.text = "KittyPress status: Choose folder to save archive"
                    pickFolderLauncher.launch(null)
                }

                inputsRoot.deleteRecursively()

            } catch (ex: Exception) {
                Log.e(TAG, "compressAction", ex)
                withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Error: ${ex.message}" }
            } finally {
                isCompressing = false
                withContext(Dispatchers.Main) { btnCompress.isEnabled = true }
            }
        }
    }

    private suspend fun runExtractToFolderInternal(archiveUri: Uri, treeUri: Uri) {
        if (isExtracting) throw IllegalStateException("Already extracting")
        isExtracting = true
        try {
            withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Running native decompress..." }

            val name = uriDisplayName(archiveUri) ?: "archive.kitty"
            val cacheArchive = File(cacheDir, "in_${System.currentTimeMillis()}_$name")
            if (cacheArchive.exists()) cacheArchive.delete()
            contentResolver.openInputStream(archiveUri)?.use { input ->
                cacheArchive.outputStream().use { output -> input.copyTo(output) }
            } ?: throw IOException("Cannot open archive for reading")

            val outDir = File(cacheDir, "out_${System.currentTimeMillis()}")
            if (outDir.exists()) outDir.deleteRecursively()
            outDir.mkdirs()

            val extractedRootName = KittyPressNative.decompressNative(cacheArchive.absolutePath, outDir.absolutePath)
            if (extractedRootName == null) {
                cacheArchive.delete()
                outDir.deleteRecursively()
                withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Decompress failed." }
                return
            }

            val extractedRoot = File(outDir, extractedRootName)
            if (!extractedRoot.exists()) {
                cacheArchive.delete()
                outDir.deleteRecursively()
                withContext(Dispatchers.Main) { statusTv.text = "KittyPress status: Extraction incomplete (no root)." }
                return
            }

            val baseFolderName = computeBaseNameForArchive(archiveUri) ?: name.substringBeforeLast('.', name)
            val targetRoot = DocumentFile.fromTreeUri(this, treeUri)
                ?: throw IOException("Destination folder not accessible (invalid tree uri)")

            val targetFolder = targetRoot.findFile(baseFolderName) ?: targetRoot.createDirectory(baseFolderName)
            if (targetFolder == null) throw IOException("Failed to create extraction folder: $baseFolderName")

            try {
                copyDirIntoDocumentFolder(extractedRoot, targetFolder)
            } catch (se: SecurityException) {
                Log.e(TAG, "Permission issue copying extracted folder", se)
                throw se
            } catch (ex: Exception) {
                Log.e(TAG, "Error copying extracted folder", ex)
                throw ex
            } finally {
                cacheArchive.delete()
                outDir.deleteRecursively()
            }

        } finally {
            isExtracting = false
        }
    }

    private fun copyDirIntoDocumentFolder(src: File, targetDir: DocumentFile) {
        if (src.isFile) {
            val name = src.name ?: "file"
            targetDir.findFile(name)?.delete()
            val created = targetDir.createFile("application/octet-stream", name)
                ?: throw IOException("Failed to create file in target folder")
            contentResolver.openOutputStream(created.uri)?.use { out ->
                src.inputStream().use { it.copyTo(out) }
            }
            return
        }

        fun recurse(curr: File, rel: String, destDir: DocumentFile) {
            curr.listFiles()?.forEach { f ->
                if (f.isDirectory) {
                    val subName = f.name ?: "dir"
                    var child = destDir.findFile(subName)
                    if (child == null) child = destDir.createDirectory(subName)
                    if (child == null) throw IOException("Failed to create directory: $subName")
                    recurse(f, if (rel.isEmpty()) subName else "$rel/$subName", child)
                } else {
                    val filename = f.name ?: "file"
                    destDir.findFile(filename)?.delete()
                    val created = destDir.createFile("application/octet-stream", filename)
                        ?: throw IOException("Failed to create file: $filename")
                    contentResolver.openOutputStream(created.uri)?.use { out ->
                        f.inputStream().use { it.copyTo(out) }
                    }
                }
            }
        }

        recurse(src, "", targetDir)
    }

    private fun copyUriToFile(uri: Uri, out: File) {
        contentResolver.openInputStream(uri)?.use { input ->
            out.outputStream().use { output -> input.copyTo(output) }
        } ?: throw IOException("Cannot open input URI")
    }

    private fun copyDocumentTreeSafely(doc: DocumentFile, dest: File) {
        if (doc.isDirectory) {
            dest.mkdirs()
            doc.listFiles().forEach { child ->
                val childName = child.name ?: "unknown"
                val childDest = File(dest, childName)
                if (child.isDirectory) {
                    childDest.mkdirs()
                    copyDocumentTreeSafely(child, childDest)
                } else {
                    contentResolver.openInputStream(child.uri)?.use { input ->
                        childDest.outputStream().use { out -> input.copyTo(out) }
                    }
                }
            }
        } else {
            val name = doc.name ?: "file"
            val outFile = File(dest, name)
            contentResolver.openInputStream(doc.uri)?.use { input ->
                outFile.outputStream().use { out -> input.copyTo(out) }
            }
        }
    }

    private fun uriDisplayName(uri: Uri): String? {
        try {
            val doc = DocumentFile.fromSingleUri(this, uri)
            if (doc?.name != null) return doc.name
        } catch (_: Exception) { }

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
        } catch (_: Exception) { }

        return uri.lastPathSegment
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
        } catch (ex: Exception) {
            Log.e(TAG, "copyFileToDocumentFolder", ex)
            false
        }
    }

    private fun hasPersistedTreePermission(treeUri: Uri): Boolean {
        return try {
            val persisted = contentResolver.persistedUriPermissions
            val targetTreeId = try { DocumentsContract.getTreeDocumentId(treeUri) } catch (_: Exception) { null }
            persisted.any { perm ->
                try {
                    if (!perm.isReadPermission || !perm.isWritePermission) return@any false
                    val candidateTreeId = try { DocumentsContract.getTreeDocumentId(perm.uri) } catch (_: Exception) { null }
                    if (targetTreeId != null && candidateTreeId != null) candidateTreeId == targetTreeId
                    else treeUri.toString().startsWith(perm.uri.toString()) || perm.uri.toString().startsWith(treeUri.toString())
                } catch (_: Exception) { false }
            }
        } catch (ex: Exception) {
            Log.w(TAG, "hasPersistedTreePermission failure", ex)
            false
        }
    }

    private fun computeBaseNameForArchive(uri: Uri?): String? {
        uri ?: return null
        try {
            val d = DocumentFile.fromSingleUri(this, uri)
            if (d?.name != null) {
                val name = d.name!!
                return if (d.isDirectory) name else name.substringBeforeLast('.', name)
            }
        } catch (_: Exception) { }
        try {
            val t = DocumentFile.fromTreeUri(this, uri)
            if (t?.name != null) return t.name!!
        } catch (_: Exception) { }
        val dn = uriDisplayName(uri) ?: return null
        return dn.substringBeforeLast('.', dn)
    }
}
