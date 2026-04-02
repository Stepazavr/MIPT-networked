using UnityEngine;
using Unity.Netcode;

public class PlayerController : NetworkBehaviour
{
    [SerializeField] private float _speed = 5f;

    private void Update()
    {
        if (!IsOwner)
            return;

        HandleMovement();
    }

    private void HandleMovement()
    {
        float horizontalInput = Input.GetAxis("Horizontal");
        float verticalInput = Input.GetAxis("Vertical");
        
        Vector3 movementDirection = new Vector3(horizontalInput, 0f, verticalInput);
        
        if (movementDirection.sqrMagnitude > 0f)
        {
            MoveServerRpc(movementDirection);
        }
    }

    [ServerRpc]
    private void MoveServerRpc(Vector3 movementDirection)
    {
        // Перемещение объекта на сервере
        Vector3 movement = movementDirection * _speed * Time.deltaTime;
        transform.position += movement;
    }
}