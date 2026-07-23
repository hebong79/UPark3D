#define DUSE_STANDALONE_FILE

using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Windows.Forms;
using SFB;
using UnityEngine;
using System.Net;
using UnityEngine.Networking;
using UnityEngine.Tilemaps;
using UnityEngine.UI;

// 라인 타입
public enum ELineType : int
{
    eNone = -1,
    eBox = 0,
    ePolygon = 1,
}
/// <summary>
/// key / value 값 클래스
/// </summary>
public class SKeyValue
{
    public int Key = 0;
    public int Value = 0;

    public SKeyValue(int key, int value)
    {
        Key = key;
        Value = value;
    }
}

/// <summary>
/// 다각형 클래스
/// </summary>
public class SPolyPoint
{
    public int m_Type = (int)ELineType.eBox;
    public List<Vector2> m_Points = new List<Vector2>();
}

[Serializable]
public class SVector2
{
    public float x;
    public float y;

    public Vector2 ToVector2()
    {
        return new Vector2(x, y);
    }
    public void Set(Vector2 v)
    {
        x = v.x;
        y = v.y;
    }

    public SVector2()
    {
    }
    public SVector2(Vector2 v)
    {
        x = (int)v.x; y = (int)v.y;
    }
}

[Serializable]
public class SVector3
{
    public float x;
    public float y;
    public float z;

    public Vector3 ToVector3()
    {
        return new Vector3(x, y, z);
    }
    public Vector3 Get()
    {
        return new Vector3(x, y, z);
    }
    public void Set(Vector3 v)
    {
        x = v.x; y = v.y; z = v.z;
    }

    public SVector3()
    {
        x = y = z = 0;
    }
    public SVector3(Vector3 v)
    {
        x = v.x; y = v.y; z = v.z;
    }

}
[Serializable]
public class SPtz
{
    public float p;     // Pan 각도
    public float t;     // Tilt 각도
    public float z;     // Zoom 레벨

    public SPtz()
    {
    }
    public SPtz(float p, float t, float z)
    {
        this.p = p; this.t = t; this.z = z;    
    }

    public void Set(float _pan, float _tilt, float _zoom)
    {
        p = _pan;
        t = _tilt;
        z = _zoom;
    }
}

[Serializable]
public class SPanTiltZoom
{
    public float pan;     // Pan 각도
    public float tilt;     // Tilt 각도
    public float zoom;     // Zoom 레벨

    public SPanTiltZoom()
    {
    }
    public SPanTiltZoom(float pan, float tilt, float zoom)
    {
        this.pan = pan; this.tilt = tilt; this.zoom = zoom;
    }

    public void Set(float _pan, float _tilt, float _zoom)
    {
        pan = _pan;
        tilt = _tilt;
        zoom = _zoom;
    }
}


public class CMyUtil
{
    /// <summary>
    /// 텍스쳐 로딩
    /// </summary>
    /// <param name="sPath"></param>
    /// <returns></returns>
    public static Texture2D LoadTexture( string sPath ) // Just doing one temporary sprite for now to learn...
    {
        //string filePath = "\\data\\graphics\\16BitSoftLogo.png";
        Texture2D texture = null;
        byte[] fileData;

        if (File.Exists(sPath))
        {
            fileData = File.ReadAllBytes(sPath);
            texture = new Texture2D(2, 2);
            texture.LoadImage(fileData);
            return texture;
        }
        return null;
    }

    /// <summary>
    /// 텍스쳐를 로딩하여 sprite를 반환 
    /// </summary>
    /// <param name="sPath"></param>
    /// <returns></returns>
    public static Sprite LoadTextureForSprite(string sPath)
    {
        //string filePath = "\\data\\graphics\\16BitSoftLogo.png";
        if (File.Exists(sPath))
        {
            byte[] fileData = File.ReadAllBytes(sPath);
            Texture2D texture = new Texture2D(2, 2);
            texture.LoadImage(fileData);

            Sprite sprite = Sprite.Create(texture, new Rect(0, 0, texture.width, texture.height), new Vector2(0.5f, 0.5f), 100.0f);

            string sName = Path.GetFileName(sPath);
            sprite.name = sName;

            Debug.LogFormat("{0}, w={1}, h={2}", sPath, texture.width, texture.height);
            return sprite;
        }
        return null;
    }

    /// <summary>
    /// 마우스 좌표를 이미지 기준 UI 좌표값으로 변환 
    /// 주의) 이미지의 RectTransform을  이용해야 한다.
    /// </summary>
    /// <param name="kCanvas"></param>
    /// <param name="kImgTransform"></param>
    /// <returns></returns>
    public static Vector2 MouseToUILocalPos(Canvas kCanvas, RectTransform kImgTransform) {
        Vector3 vPos = Input.mousePosition;
        Camera kCamera = kCanvas.worldCamera;

        Vector2 vLocal;
        RectTransformUtility.ScreenPointToLocalPointInRectangle(kImgTransform, vPos, kCamera, out vLocal);

        return  vLocal;
    }

    /// <summary>
    /// 마우스좌표를 이미지 기준 좌표로 변환 
    /// </summary>
    /// <param name="kCanvas"></param>
    /// <param name="kImgTransform"></param>
    /// <param name="imgW"></param>
    /// <param name="imgH"></param>
    /// <returns></returns>
    public static Vector2 MouseToImagePixelPos(Canvas kCanvas, RectTransform kImgTransform, float imgW = 1920, float imgH = 1080) {

        Vector2 vLocal = MouseToUILocalPos(kCanvas, kImgTransform);
        //float imgW = kImgTransform.rect.width;
        //float imgH = kImgTransform.rect.height;

        //Debug.Log($"Image W = {imgW}, H = {imgH}");

        // 이미지 기준으로 좌표 변경(좌상단이(0, 0))
        float x = vLocal.x + imgW * 0.5f;
        float y = (imgH - vLocal.y) - imgH * 0.5f;
        return new Vector2(x, y);
    }

    //public static Vector2 ScreenToImagePixelPos(Vector2 vMousePos, RectTransform kImgTransform) {
    //    // 스크린 좌표를 월드 좌표로 변환
    //    Vector2 localPoint;
    //    RectTransformUtility.ScreenPointToLocalPointInRectangle(kImgTransform, vMousePos, null, out localPoint);

    //    // 월드 좌표를 이미지 로컬 좌표로 변환
    //    return localPoint; ;
    //}

    /// <summary>
    /// 이미지 기준 position -> Screen Position 으로 변경
    /// </summary>
    /// <param name="vPixel"></param>
    /// <param name="fImgW"></param>
    /// <param name="fImgH"></param>
    /// <returns></returns>
    public static Vector2 ImagePixelPosToScreenPos( Vector2 vPixel, float fImgW =1920, float fImgH = 1080 ) {
        float fScrW = UnityEngine.Screen.width;
        float fScrH = UnityEngine.Screen.height;

        Vector2 vMouse = vPixel;
        vMouse.y = fImgH - vPixel.y;

        float x = vMouse.x * fScrW / fImgW;
        float y = vMouse.y * fScrH / fImgH;

        return new Vector2(x, y);
    }

    public static Vector2 ImagePixelPosToScreenPos(Vector2 vPixel, RectTransform kImgTransform) {
        // 이미지의 좌표를 월드 좌표로 변환
        Vector2 worldPosition;
        RectTransformUtility.ScreenPointToLocalPointInRectangle(kImgTransform, vPixel, null, out worldPosition);

        // 월드 좌표를 스크린 좌표로 변환
        Vector2 screenPosition = RectTransformUtility.WorldToScreenPoint(null, kImgTransform.TransformPoint(worldPosition));
        return screenPosition;
    }

    /// <summary>
    /// 파일 열기 대화상자
    /// </summary>
    /// <param name="filter"></param>
    /// <returns></returns>
    public static string FileOpen(string filter = "All files  (*.*)|*.*")
    {
        OpenFileDialog kOpenDialog = new OpenFileDialog();
        kOpenDialog.Filter = filter;
        kOpenDialog.FilterIndex = 3;
        kOpenDialog.Title = "File Dialog";

        kOpenDialog.Filter = filter;
        if (kOpenDialog.ShowDialog() == DialogResult.OK)
        {
            Stream openStream = null;
            if ((openStream = kOpenDialog.OpenFile()) != null)
            {
                openStream.Close();
                return kOpenDialog.FileName;
            }
        }
        return "";
    }

    /// <summary>
    /// UUID 생성 메소드 
    /// </summary>
    /// <returns></returns>
    public static string GenerateUUID() {
        // 새 GUID 생성
        Guid guid = Guid.NewGuid();

        // 문자열 형태로 변환하여 반환
        return guid.ToString();
    }

    // 패스워드 암호화
    public static byte[] EncryptPassword(string plainPassword, byte[] aesKey, byte[] aesIV) {
        if (plainPassword == null || plainPassword.Length <= 0)
            throw new ArgumentNullException("plainPassword");
        if (aesKey == null || aesKey.Length <= 0)
            throw new ArgumentNullException("Key");
        if (aesIV == null || aesIV.Length <= 0)
            throw new ArgumentNullException("IV");

        byte[] encrypted;

        // AES 암호화 객체 생성
        using (Aes aesAlg = Aes.Create()) {
            aesAlg.Key = aesKey;
            aesAlg.IV = aesIV;

            // 암호화 수행
            ICryptoTransform encryptor = aesAlg.CreateEncryptor(aesAlg.Key, aesAlg.IV);
            using (MemoryStream msEncrypt = new MemoryStream()) {
                using (CryptoStream csEncrypt = new CryptoStream(msEncrypt, encryptor, CryptoStreamMode.Write)) {
                    using (StreamWriter swEncrypt = new StreamWriter(csEncrypt)) {
                        swEncrypt.Write(plainPassword);
                    }
                    encrypted = msEncrypt.ToArray();
                }
            }
        }


        // 암호화된 패스워드를 PlayerPrefs에 저장
        PlayerPrefs.SetString("encryptedPassword", Convert.ToBase64String(encrypted));
        return encrypted;
    }

    // 패스워드 복호화
    public static string DecryptPassword(byte[] encryptedPassword, byte[] aesKey, byte[] aesIV) {
        if (encryptedPassword == null || encryptedPassword.Length <= 0)
            throw new ArgumentNullException("encryptedPassword");
        if (aesKey == null || aesKey.Length <= 0)
            throw new ArgumentNullException("Key");
        if (aesIV == null || aesIV.Length <= 0)
            throw new ArgumentNullException("IV");

        string decryptedPassword;

        // AES 복호화 객체 생성
        using (Aes aesAlg = Aes.Create()) {
            aesAlg.Key = aesKey;
            aesAlg.IV = aesIV;

            // 복호화 수행
            ICryptoTransform decryptor = aesAlg.CreateDecryptor(aesAlg.Key, aesAlg.IV);
            using (MemoryStream msDecrypt = new MemoryStream(encryptedPassword)) {
                using (CryptoStream csDecrypt = new CryptoStream(msDecrypt, decryptor, CryptoStreamMode.Read)) {
                    using (StreamReader srDecrypt = new StreamReader(csDecrypt)) {
                        decryptedPassword = srDecrypt.ReadToEnd();
                    }
                }
            }
        }

        return decryptedPassword;
    }

    public static string aesKeyPath = "aes_key.bin";

    public static void EncryptDBData(string pass, ref string sPassEncrypt, ref string sIVEncrypt) {
        // 파일에서 AES 키와 IV를 불러옴
        byte[] aesKey = File.ReadAllBytes(aesKeyPath);

        Aes aesAlg = Aes.Create();
        aesAlg.Key = aesKey;
        byte[] encryptedPassword = EncryptPassword(pass, aesAlg.Key, aesAlg.IV);

        sPassEncrypt = Convert.ToBase64String(encryptedPassword);
        sIVEncrypt = Convert.ToBase64String(aesAlg.IV);
    }

    public static string DecryptDBData(string passEncrypt64, string ivEncrypt64) {

        byte[] aesKey = File.ReadAllBytes(aesKeyPath);
        byte[] aesIV = Convert.FromBase64String(ivEncrypt64);

        byte[] encryptedPassword = Convert.FromBase64String(passEncrypt64);

        // 복호화된 패스워드 출력
        string pass = DecryptPassword(encryptedPassword, aesKey, aesIV);
        Debug.Log("Decrypted Password: " + pass);
        return pass;
    }

    /// <summary>
    /// base64String --> Texture2D 변환
    /// </summary>
    /// <param name="base64String"></param>
    /// <returns></returns>
    public static Texture2D Base64ToTexture(string base64String)
    {
        // Convert Base64 String to byte[]
        byte[] imageBytes = Convert.FromBase64String(base64String);

        Texture2D texture = new Texture2D(2, 2);
        texture.LoadImage(imageBytes);
        return texture;
    }


    // Texture2D를 파일로 저장하는 함수
    public static void SaveTextureToFile(Texture2D texture, string basePath, string fileName)
    {
        // Texture2D를 JPG 형식의 바이트 배열로 변환
        byte[] imageBytes = texture.EncodeToJPG();

        // 파일을 저장할 경로 지정
        string filePath = Path.Combine(basePath, fileName);
        
        // ✅ 수정: 디렉토리가 없으면 생성
        if (!Directory.Exists(basePath))
        {
            Directory.CreateDirectory(basePath);
        }

        try
        {
            // 바이트 배열을 파일로 저장
            File.WriteAllBytes(filePath, imageBytes);
            Debug.Log($"이미지가 파일로 저장되었습니다: {filePath}");
        }
        catch (Exception ex)
        {
            Debug.Log("파일저장 실패 : " + ex);
        }
    }

    // 텍스쳐 파일 로딩하기 ( 프로젝트 외의 폴더 )
    public static Texture2D LoadTextureFromFile(string basePath, string fileName)
    {
        try
        {
            string filePath = Path.Combine(basePath, fileName);
            byte[] imageBytes = File.ReadAllBytes(filePath);

            Texture2D texture = new Texture2D(2, 2);
            texture.LoadImage(imageBytes);
            return texture;

        }
        catch (Exception ex)
        {
            Debug.Log("파일열기 실패 : " + ex);
        }
        return null;
    }


    // 현재 시간을 기준으로 파일이름 얻기
    // path : 패쓰
    // postfixName : 후미에 붙을 이름,
    // ext : 확장자
    public static string GetFileNameFromCurDate(string path, string postfixName, string ext)
    {
        // 현재 시간을 가져옵니다.
        System.DateTime now = System.DateTime.Now;

        // 포맷: "년도_월_일_시_분_초"로 파일 이름 생성
        string fileName = $"{now.Year}{now.Month:D2}{now.Day:D2}_{now.Hour:D2}{now.Minute:D2}{now.Second:D2}";

        string fileName2 = $"{fileName}{postfixName}.{ext}";
        string pathName = Path.Combine(path, fileName2);

        return pathName;
    }

    // 폴더에 있는 파일이름 얻어오기
    public static string[] GetFileListInFolder(string sFolderPath)
    {
        // 경로가 실제로 존재하는지 확인
        if (Directory.Exists(sFolderPath))
        {
            // 폴더 내 모든 파일 경로 가져오기
            string[] files = Directory.GetFiles(sFolderPath);

            //// 파일 이름만 추출해서 출력
            //foreach (string filePath in files)
            //{
            //    string fileName = Path.GetFileName(filePath);
            //    Debug.Log("파일 이름: " + fileName);
            //}
            return files;
        }
        else
        {
            Debug.LogWarning("폴더가 존재하지 않습니다: " + sFolderPath);
        }
        return null;
    }


    /// <summary>
    /// - 주의: WebGL에서는 사용할수 없다.
    /// 윈도우 환경에서만 사용하자.
    /// </summary>
    /// <returns></returns>
    public static string GetLocalIPAddress()
    {
        string localIP = "알 수 없음";
        try
        {
            string hostName = Dns.GetHostName(); // 호스트 이름 가져오기
            IPAddress[] addresses = Dns.GetHostAddresses(hostName);

            foreach (IPAddress ip in addresses)
            {
                if (ip.AddressFamily == System.Net.Sockets.AddressFamily.InterNetwork)
                {
                    localIP = ip.ToString();
                    break;
                }
            }
        }
        catch (Exception e)
        {
            Debug.LogError("IP 주소를 가져오는 중 오류 발생: " + e.Message);
        }

        return localIP;
    }


    /// <summary>
    /// 파일 오픈 다이얼로그
    /// </summary>
    /// <param name="baseDir"></param>
    /// <param name="sFilterName"></param>
    /// <param name="exts"></param>
    /// <returns></returns>
    public static string StandaloneFileOpen(string baseDir, string sFilterName = "Json Files", params string[] exts)
    {
//#if UNITY_STANDALONE_WIN //  DUSE_STANDALONE_FILE      
        // Open file with filter
        var extensions = new[] {
            new ExtensionFilter(sFilterName, exts ),
            new ExtensionFilter("All Files", "*" ),
        };
        var paths = StandaloneFileBrowser.OpenFilePanel("Open File", baseDir, extensions, false);
        if (paths == null || paths.Length == 0) return "";

        return paths[0];
//#else
//        return FileOpen("Json files (*.json)|*.json|All files (*.*)|*.*");      
//#endif
    }

    /// <summary>
    /// 파일 오픈 다이얼로그
    /// </summary>
    /// <param name="baseDir"></param>
    /// <param name="sFilterName"></param>
    /// <param name="exts"></param>
    /// <returns>패스+파일이름</returns>
    public static string StandaloneFileSave(string baseDir, string defaultFileName, string sFilterName = "Json Files", params string[] exts)
    {
//#if UNITY_STANDALONE_WIN// DUSE_STANDALONE_FILE      
        // Open file with filter
        var extensions = new[] {
            new ExtensionFilter(sFilterName, exts ),
            new ExtensionFilter("All Files", "*" ),
        };
        var paths = StandaloneFileBrowser.SaveFilePanel("Save File", baseDir, defaultFileName, extensions);
        if (paths == null || paths.Length == 0) return "";

        return paths;
//#else
//        return FileOpen("Json files (*.json)|*.json|All files (*.*)|*.*");      
//#endif
    }

    /// <summary>
    /// Texture2D를 바이트 배열로 변환합니다.
    /// asJpg가 true이면 JPG(jpgQuality 사용), 아니면 PNG로 인코딩합니다.
    /// 입력 텍스처가 읽기 불가능한 경우 내부에서 읽기 가능한 복사본을 생성해 처리합니다.
    /// 실패 시 null 반환.
    /// </summary>
    /// <param name="source">변환할 Texture2D</param>
    /// <param name="isJpg">JPG로 인코딩할지 여부 (기본 false = PNG)</param>
    /// <param name="jpgQuality">JPG 품질(1-100, 기본 75)</param>
    /// <returns>이미지 바이트 배열 또는 null</returns>
    public static byte[] TextureToByteArray(Texture2D source, bool isJpg = true, int jpgQuality = 75)
    {
        if (source == null)
        {
            Debug.LogWarning("CPSimGameUI: TextureToByteArray - source is null");
            return null;
        }

        Texture2D readable = source;

        // 텍스처가 읽기 가능한지 확인. Unity 에디터/런타임에서 isReadable가 false일 수 있음.
        bool createdCopy = false;
        if (!IsTextureReadable(source))
        {
            readable = CreateReadableCopy(source);
            if (readable == null)
            {
                Debug.LogError("CPSimGameUI: Failed to create readable copy of texture.");
                return null;
            }
            createdCopy = true;
        }

        byte[] bytes;
        try
        {
            if (isJpg)
                bytes = readable.EncodeToJPG(Mathf.Clamp(jpgQuality, 1, 100));
            else
                bytes = readable.EncodeToPNG();
        }
        catch (System.Exception ex)
        {
            Debug.LogError($"CPSimGameUI: Encoding texture failed: {ex.Message}");
            bytes = null;
        }

        if (createdCopy && readable != null)
        {
            // 런타임에 생성한 임시 텍스처는 해제
            GameObject.Destroy(readable);
        }

        return bytes;
    }

    /// <summary>
    /// Texture2D를 Base64 문자열로 변환합니다.
    /// asJpg가 true이면 JPG로 인코딩 후 base64, 아니면 PNG로 인코딩 후 base64를 반환합니다.
    /// includeDataUri가 true이면 data URI 접두사 (예: "data:image/png;base64,")를 포함합니다.
    /// 실패 시 null 반환.
    /// </summary>
    /// <param name="source">변환할 Texture2D</param>
    /// <param name="asJpg">JPG로 인코딩할지 여부 (기본 false = PNG)</param>
    /// <param name="jpgQuality">JPG 품질(1-100, 기본 75)</param>
    /// <param name="includeDataUri">data URI 접두사를 포함할지 여부</param>
    public static string TextureToBase64(Texture2D source, bool asJpg = false, int jpgQuality = 75, bool includeDataUri = false)
    {
        var bytes = TextureToByteArray(source, asJpg, jpgQuality);
        if (bytes == null)
            return null;

        string b64 = Convert.ToBase64String(bytes);
        if (includeDataUri)
        {
            string mime = asJpg ? "image/jpeg" : "image/png";
            return $"data:{mime};base64,{b64}";
        }
        return b64;
    }

    // Texture2D.isReadable 사용 가능 여부를 안전하게 확인
    public static bool IsTextureReadable(Texture2D tex)
    {
        try
        {
            return tex.isReadable;
        }
        catch
        {
            // 어떤 플랫폼/포맷에서는 접근이 예외를 던질 수 있으므로 false로 처리
            return false;
        }
    }

    // 읽기 불가능한 Texture2D를 RenderTexture를 통해 읽기 가능한 복사본(Texture2D)으로 생성
    public static Texture2D CreateReadableCopy(Texture2D src)
    {
        if (src == null)
            return null;

        int width = src.width;
        int height = src.height;

        // 임시 RenderTexture 생성
        RenderTexture rt = RenderTexture.GetTemporary(width, height, 0, RenderTextureFormat.Default, RenderTextureReadWrite.Linear);
        RenderTexture prev = RenderTexture.active;

        // Blit 원본(텍스처 또는 텍스처가 붙은 머티리얼) -> RT
        // Graphics.Blit accepts Texture as source
        Graphics.Blit(src, rt);

        RenderTexture.active = rt;
        Texture2D copy = new Texture2D(width, height, TextureFormat.RGBA32, false);
        try
        {
            copy.ReadPixels(new Rect(0, 0, width, height), 0, 0);
            copy.Apply();
        }
        catch (System.Exception ex)
        {
            Debug.LogError($"CPSimGameUI: CreateReadableCopy ReadPixels failed: {ex.Message}");
            RenderTexture.active = prev;
            RenderTexture.ReleaseTemporary(rt);
            GameObject.Destroy(copy);
            return null;
        }

        RenderTexture.active = prev;
        RenderTexture.ReleaseTemporary(rt);

        return copy;
    }
}
