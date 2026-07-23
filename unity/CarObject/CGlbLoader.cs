using System;
using System.Collections;
using System.Threading.Tasks;
using GLTFast;
using UnityEngine;

/// <summary>
/// GLB 파일을 런타임에 로드하는 유틸리티 클래스.
/// Resources 폴더 또는 절대 경로에서 GLB를 비동기 로드한다.
/// </summary>
public class CGlbLoader : MonoBehaviour
{
    // 마지막으로 로드된 루트 GameObject (테스트/디버깅용)
    public GameObject LastLoadedRoot { get; private set; }

    /// <summary>
    /// Resources 폴더 기준 상대 경로로 GLB를 비동기 로드한다.
    /// 성공 시 onSuccess(rootGO) 콜백, 실패 시 onFail(error) 콜백을 호출한다.
    /// </summary>
    /// <param name="resourcePath">Resources 기준 경로 (확장자 포함 OK, 없어도 OK)</param>
    /// <param name="parent">로드된 오브젝트를 붙일 부모 Transform (null이면 씬 루트)</param>
    /// <param name="onSuccess">성공 콜백</param>
    /// <param name="onFail">실패 콜백</param>
    public void LoadFromResources(string resourcePath, Transform parent,
        Action<GameObject> onSuccess, Action<string> onFail = null)
    {
        StartCoroutine(Coroutine_LoadFromResources(resourcePath, parent, onSuccess, onFail));
    }

    private IEnumerator Coroutine_LoadFromResources(string resourcePath, Transform parent,
        Action<GameObject> onSuccess, Action<string> onFail)
    {
        // Resources.LoadAsync는 GLB 바이너리를 TextAsset으로 읽을 수 없으므로
        // Resources.Load<TextAsset>로 bytes를 확보 후 GltfImport에 넘긴다.
        string pathNoExt = resourcePath.EndsWith(".glb", StringComparison.OrdinalIgnoreCase)
            ? resourcePath.Substring(0, resourcePath.Length - 4)
            : resourcePath;

        TextAsset asset = Resources.Load<TextAsset>(pathNoExt);
        if (asset == null)
        {
            string err = $"[CGlbLoader] Resources.Load 실패: {pathNoExt}";
            Debug.LogError(err);
            onFail?.Invoke(err);
            yield break;
        }

        yield return LoadFromBytes(asset.bytes, parent, onSuccess, onFail);
    }

    /// <summary>
    /// 파일 시스템 절대 경로의 GLB를 비동기 로드한다.
    /// </summary>
    public void LoadFromFilePath(string absolutePath, Transform parent,
        Action<GameObject> onSuccess, Action<string> onFail = null)
    {
        StartCoroutine(Coroutine_LoadFromFilePath(absolutePath, parent, onSuccess, onFail));
    }

    private IEnumerator Coroutine_LoadFromFilePath(string absolutePath, Transform parent,
        Action<GameObject> onSuccess, Action<string> onFail)
    {
        if (!System.IO.File.Exists(absolutePath))
        {
            string err = $"[CGlbLoader] 파일 없음: {absolutePath}";
            Debug.LogError(err);
            onFail?.Invoke(err);
            yield break;
        }

        byte[] bytes = null;
        try { bytes = System.IO.File.ReadAllBytes(absolutePath); }
        catch (Exception e)
        {
            string err = $"[CGlbLoader] 파일 읽기 실패: {absolutePath}\n{e.Message}";
            Debug.LogError(err);
            onFail?.Invoke(err);
            yield break;
        }

        yield return LoadFromBytes(bytes, parent, onSuccess, onFail);
    }

    /// <summary>
    /// GLB 바이트 배열을 직접 받아 로드한다.
    /// </summary>
    public IEnumerator LoadFromBytes(byte[] glbBytes, Transform parent,
        Action<GameObject> onSuccess, Action<string> onFail = null)
    {
        if (glbBytes == null || glbBytes.Length == 0)
        {
            string err = "[CGlbLoader] glbBytes가 null이거나 비어있음";
            Debug.LogError(err);
            onFail?.Invoke(err);
            yield break;
        }

        var gltf = new GltfImport();
        Task<bool> loadTask = gltf.LoadGltfBinary(glbBytes);

        yield return new WaitUntil(() => loadTask.IsCompleted);

        if (!loadTask.Result)
        {
            string err = "[CGlbLoader] GltfImport.LoadGltfBinary 실패";
            Debug.LogError(err);
            onFail?.Invoke(err);
            gltf.Dispose();
            yield break;
        }

        GameObject root = new GameObject("GlbRoot");
        if (parent != null) root.transform.SetParent(parent, false);

        Task<bool> instantiateTask = gltf.InstantiateMainSceneAsync(root.transform);
        yield return new WaitUntil(() => instantiateTask.IsCompleted);

        if (!instantiateTask.Result)
        {
            string err = "[CGlbLoader] GltfImport.InstantiateMainSceneAsync 실패";
            Debug.LogError(err);
            onFail?.Invoke(err);
            Destroy(root);
            gltf.Dispose();
            yield break;
        }

        gltf.Dispose();
        LastLoadedRoot = root;
        onSuccess?.Invoke(root);
    }
}
