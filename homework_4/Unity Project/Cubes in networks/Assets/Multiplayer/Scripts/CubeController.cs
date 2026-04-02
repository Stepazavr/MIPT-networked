using UnityEngine;
using Unity.Netcode;

public class CubeController : NetworkBehaviour
{
    [SerializeField] private float moveSpeed = 5f;
    [SerializeField] private float moveRange = 3f;
    [SerializeField] private bool is_interpolated = false;
    [SerializeField] private bool is_extrapolated = false;


    private NetworkVariable<float> _networkSpeed = new NetworkVariable<float>(
            2f,
            NetworkVariableReadPermission.Everyone,
            NetworkVariableWritePermission.Server
        );
    private NetworkVariable<Vector3> _networkPosition = new NetworkVariable<Vector3>(
            Vector3.zero,
            NetworkVariableReadPermission.Everyone,
            NetworkVariableWritePermission.Server
        );

    private Vector3 _startPosition;
    private Vector3 _currentPosition;
    private Vector3 _futurePosition;
    private float timePercent;

    public override void OnNetworkSpawn()
    {
        _startPosition = transform.position;

        if (IsServer)
        {
            _networkSpeed.Value = moveSpeed;
            _networkPosition.Value = transform.position;
        }
        else
        {
            _futurePosition = transform.position;
            _currentPosition = transform.position;
            timePercent = 0f;

            _networkPosition.OnValueChanged += OnPositionChanged;
        }

    }

    private void OnPositionChanged(Vector3 oldValue, Vector3 newValue)
    {
        if (is_interpolated)
        {
            timePercent = 0.0f;
            _currentPosition = _futurePosition;
            _futurePosition = newValue;
        }

        else
        {
            transform.position = newValue;
        }
    }

    private void Update()
    {
        if (IsServer)
        {

            if (transform.position.y >= _startPosition.y + moveRange)
            {

                _networkSpeed.Value = -Mathf.Abs(_networkSpeed.Value);
            }
            else if (transform.position.y <= _startPosition.y - moveRange)
            {

                _networkSpeed.Value = Mathf.Abs(_networkSpeed.Value);
            }

            transform.position += new Vector3(0, _networkSpeed.Value * Time.deltaTime, 0);
            _networkPosition.Value = transform.position;
        }

        else if (is_interpolated)
        {  
            transform.position = Vector3.Lerp(_currentPosition, _futurePosition, timePercent);
            timePercent += Time.deltaTime * NetworkManager.Singleton.NetworkConfig.TickRate;
        }
        else if (is_extrapolated)
        {
            transform.position += new Vector3(0, _networkSpeed.Value * Time.deltaTime, 0);
        }
    }
}