using UnityEngine;
using Unity.Netcode;

public class CubeSpawner : NetworkBehaviour
{
    [SerializeField] private GameObject[] _cubePrefabs;
    [SerializeField] private Vector3 _offset = new Vector3(5, 0, 0);

    public override void OnNetworkSpawn()
    {
        if (!IsServer) return;

        for (int i = 0; i < _cubePrefabs.Length; i++)
        {
            Vector3 spawnPosition = new Vector3(-5, 0, 0) + i * _offset;

            GameObject cube = Instantiate(_cubePrefabs[i], spawnPosition, Quaternion.identity);
            cube.GetComponent<NetworkObject>().Spawn();

        }
    }
}