#include <WiFi.h>
#include <WebServer.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>

// ================= WIFI CREDENTIALS =================
// Yahan apne Wi-Fi ya phone hotspot ka naam (SSID) aur password daalo
const char* ssid = "Shukla";
const char* password = "apn45979";

// ================= SD CARD PINS (30-PIN ESP32) =================
// ESP32 (30-pin) ke standard SPI pins define kiye hain
// CRITICAL NOTE: SD module ka VCC hamesha VIN (5V) mein lagana hai, 3.3V mein nahi!
#define SD_CS   5
#define SD_SCK  18
#define SD_MOSI 23
#define SD_MISO 19

// Port 80 (standard HTTP port) par web server banaya
WebServer server(80);

// Jab file upload hogi, toh ye object SD card mein data write karne ke kaam aayega
File uploadFile;

// ================= INLINE HTML + CSS + JS =================
// PROGMEM aur R"rawliteral(...)rawliteral" ka use isliye kiya hai taaki poora HTML/CSS 
// ESP32 ki Flash memory (hard drive) mein save ho, RAM mein jagah na ghere.
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>MyNAS</title>
    <style>
        /* Custom Dark Mode UI styling */
        body { margin: 0px; background-color: rgb(17, 17, 17); color: rgb(101, 187, 101); }
        #header { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; text-align: center; padding: 15px; margin: 0px; font-size: 50px; background-color: black; }
        h2 { font-family: Arial, sans-serif; background-color: black; text-align: center; padding: 10px; margin: 0px; font-size: 14px; color: rgb(101, 187, 101); box-shadow: 5px 5px 10px rgba(0, 0, 0, 0.5); }
        .upload_buttons { display: flex; justify-content: center; align-items: center; gap: 15px; margin: 40px auto; padding: 25px 25px; width: fit-content; background-color: rgba(0, 0, 0, 0.8); border: 1px solid rgb(26, 26, 26); border-radius: 30px; box-shadow: 2px 2px 10px rgba(0, 0, 0, 0.5); }
        .hidden_input { display: none; }
        .custom_file_btn { background-color: #2c2c2e; color: rgb(101, 187, 101); padding: 10px 20px; border-radius: 10px; cursor: pointer; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; font-size: 14px; font-weight: 500; }
        .file_name_text { color: rgb(101, 187, 101); font-size: 14px; font-family: monospace; max-width: 150px; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
        .upload_btn { background-color: rgb(101, 187, 101); color: white; padding: 10px 20px; border-radius: 10px; cursor: pointer; border: none; font-weight: 600; transition: 0.2s; }
        .upload_btn:disabled { opacity: 0.5; cursor: not-allowed; }
        .file-list-container { max-width: 500px; margin: 30px auto; padding: 0 20px; }
        .file-list { background-color: black; border-radius: 16px; overflow: hidden; box-shadow: 0 4px 20px rgba(0, 0, 0, 0.4); }
        .file-item { display: flex; justify-content: space-between; align-items: center; padding: 15px; }
        .file-info { display: flex; align-items: center; gap: 15px; }
        .file-details { display: flex; flex-direction: column; }
        .file-name { color: #ffffff; font-size: 15px; font-weight: 500; }
        .download-btn { border: none; background-color: rgb(101, 187, 101); color: white; padding: 6px 14px; border-radius: 20px; font-size: 12px; font-weight: 600; cursor: pointer; text-decoration: none; }
    </style>
</head>
<body>
    <h1 id="header">MyNAS</h1>
    <h2>(made by Neelansh Shukla)</h2>
    
    <!-- File Upload Section -->
    <div class="upload_buttons">
        <input type="file" id="file_Input" class="hidden_input">
        <label for="file_Input" class="custom_file_btn">Choose File</label>
        <span id="file_name_text" class="file_name_text">No file chosen</span>
        <button id="upload_btn" class="upload_btn">Upload</button>
    </div>
    
    <!-- File List Section (JS se populate hoga) -->
    <div class="file-list-container">
        <div id="file-list" class="file-list"></div>
    </div>

    <script>
        // HTML elements ko JS variables mein grab kar rahe hain
        const fileInput = document.getElementById('file_Input');
        const fileNameText = document.getElementById('file_name_text');
        const uploadBtn = document.getElementById('upload_btn');
        const fileList = document.getElementById('file-list');

        // Jab user 'Choose File' click karke koi file select kare, toh uska naam screen par dikhao
        fileInput.addEventListener('change', () => {
            fileNameText.textContent = fileInput.files.length > 0 ? fileInput.files[0].name : 'No file chosen';
        });

        // ESP32 ke '/list' route se SD card ki saari files mangwane wala function
        async function loadFiles() {
            try {
                const res = await fetch('/list'); // ESP32 ko GET request bheji
                const files = await res.json();   // ESP32 ne JSON format mein file list bheji
                fileList.innerHTML = '';          // Purani list clear ki

                // Agar SD card khali hai toh message dikhao
                if (files.length === 0) {
                    fileList.innerHTML = '<div class="file-item"><span class="file-name">No files on SD card</span></div>';
                    return;
                }

                // Har file ke liye HTML div create karke list mein add karo
                files.forEach(file => {
                    const item = document.createElement('div');
                    item.className = 'file-item';
                    item.innerHTML = `
                        <div class="file-info"><div class="file-details"><span class="file-name">${file.name}</span></div></div>
                        <a href="/download?file=${encodeURIComponent(file.name)}" class="download-btn" download>Download</a>
                    `;
                    fileList.appendChild(item);
                });
            } catch (err) { console.error('Error:', err); }
        }

        // Jab 'Upload' button dabaye toh file ko ESP32 ke '/upload' route par bhejo
        uploadBtn.addEventListener('click', async () => {
            if (fileInput.files.length === 0) { alert('Select a file first!'); return; }
            
            const formData = new FormData();
            formData.append('file', fileInput.files[0]); // File ko multipart form data mein pack kiya

            // Upload ke dauran button disable kar do taaki double click na ho
            uploadBtn.disabled = true;
            uploadBtn.textContent = 'Uploading...';
            
            try {
                // ESP32 ko POST request ke saath file bheji
                const res = await fetch('/upload', { method: 'POST', body: formData });
                if (res.ok) { 
                    fileInput.value = ''; 
                    fileNameText.textContent = 'No file chosen'; 
                    loadFiles(); // Upload hone ke baad list refresh karo
                }
                else { alert('Upload failed.'); }
            } catch (err) { alert('Upload error.'); }
            finally { 
                // Button ko wapas normal state mein laao
                uploadBtn.disabled = false; 
                uploadBtn.textContent = 'Upload'; 
            }
        });
        
        // Jaise hi webpage open ho, turant SD card ki files load kar lo
        window.onload = loadFiles;
    </script>
</body>
</html>
)rawliteral";

// ================= BACKEND HANDLERS (C++) =================

// 1. Jab koi browser mein ESP32 ka IP address khole ('/'), toh ye main webpage bhej dega
void handleRoot() { 
    // send_P ka matlab hai Flash Memory (PROGMEM) se seedha page read karke bhejo
    server.send_P(200, "text/html", index_html); 
}

// 2. SD Card ke root folder ("/") ko scan karke saari files ka JSON banana
void handleFileList() {
    File root = SD.open("/");
    if (!root || !root.isDirectory()) { server.send(500, "application/json", "[]"); return; }
    
    String json = "[";
    File file = root.openNextFile();
    bool first = true;
    
    // Jab tak SD card mein agali file milti rahe, loop chalao
    while (file) {
        if (!file.isDirectory()) {
            String fileName = String(file.name());
            // Windows/Mac jo chhupe hue system files bana dete hain unhe ignore karo
            if (!fileName.startsWith(".") && !fileName.startsWith("System Volume")) {
                if (!first) json += ",";
                json += "{\"name\":\"" + fileName + "\",\"size\":" + String(file.size()) + "}";
                first = false;
            }
        }
        file = root.openNextFile(); // Agali file par jao
    }
    json += "]";
    server.send(200, "application/json", json);
}

// 3. Jab user 'Download' button click kare, toh SD card se file browser mein stream karo
void handleFileDownload() {
    if (!server.hasArg("file")) { server.send(400, "text/plain", "Missing file parameter"); return; }
    
    String fileName = server.arg("file");
    if (!fileName.startsWith("/")) fileName = "/" + fileName; // Path ko '/' se start karo
    
    if (!SD.exists(fileName)) { server.send(404, "text/plain", "File not found"); return; }
    
    // File ko read mode mein open karo
    File downloadFile = SD.open(fileName, FILE_READ);
    if (!downloadFile) { server.send(500, "text/plain", "Failed to open file"); return; }
    
    // 'Content-Disposition: attachment' browser ko bolta hai ki file ko download kare
    String cleanName = fileName.substring(1);
    server.sendHeader("Content-Disposition", "attachment; filename=\"" + cleanName + "\"");
    
    // File ko chunks mein browser ko stream kar do aur phir close kar do
    server.streamFile(downloadFile, "application/octet-stream");
    downloadFile.close();
}

// 4. Jab user file upload kare, toh badi file ko chhote-chhote tukdon (chunks) mein SD card par write karo
void handleFileUpload() {
    HTTPUpload& upload = server.upload();
    
    // STEP A: Upload shuru hua -> SD card par nayi blank file create karo
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!filename.startsWith("/")) filename = "/" + filename;
        uploadFile = SD.open(filename, FILE_WRITE);
    } 
    // STEP B: Data ke tukde (chunks) aa rahe hain -> unhe SD card mein write karte jao
    else if (upload.status == UPLOAD_FILE_WRITE) {
        if (uploadFile) uploadFile.write(upload.buf, upload.currentSize);
    } 
    // STEP C: Poori file aa gayi -> file ko safely close karke save kar do
    else if (upload.status == UPLOAD_FILE_END) {
        if (uploadFile) { uploadFile.close(); }
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    // 1. SD Card Initialization
    Serial.println("Initializing SD Card...");
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    if (!SD.begin(SD_CS)) {
        Serial.println("SD Card Mount Failed!");
    } else {
        Serial.println("SD Card Mounted Successfully!");
    }

    // 2. Wi-Fi Connection
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    
    Serial.println("\nWi-Fi Connected!");
    Serial.print("Open this link in your browser: http://");
    Serial.println(WiFi.localIP()); // Serial Monitor par IP address print karega

    // 3. URL Routes (Konse link par konsa C++ function chalega)
    server.on("/", HTTP_GET, handleRoot);
    server.on("/list", HTTP_GET, handleFileList);
    server.on("/download", HTTP_GET, handleFileDownload);
    
    // Upload route mein do functions hain: pehla jab upload khatam ho jaye tab browser ko "200 OK" bhejne ke liye,
    // aur doosra 'handleFileUpload' jo aane wale file chunks ko SD card mein write karta hai.
    server.on("/upload", HTTP_POST, []() { server.send(200, "text/plain", "Upload Success"); }, handleFileUpload);

    server.begin();
}

void loop() {
    // Ye function lagataar check karta rehta hai ki kisi browser se koi request aayi hai kya
    server.handleClient();
}