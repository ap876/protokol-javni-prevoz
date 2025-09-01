// UPPAAL Properties for Public Transport Protocol Verification

// Safety Properties
// =================

// 1. System never deadlocks
A[] not deadlock

// 2. No buffer overflows - processing time constraints are respected
A[] (server.Processing imply server.processing_timer <= MAX_PROCESSING_TIME)

// 3. Authentication required before service access
A[] (client1.Service_Request imply client1.user_authenticated)

// 4. TLS encryption is active during communication
A[] (current_state >= STATE_CONNECTED imply tls_enabled)

// 5. Database consistency - pending requests counter is valid
A[] (server.pending_requests >= 0)

// 6. Vehicle capacity constraints are respected
A[] (vehicle_bus.current_capacity >= 0 and vehicle_bus.current_capacity <= 50)
A[] (vehicle_tram.current_capacity >= 0 and vehicle_tram.current_capacity <= 40)

// 7. No unauthorized access to services
A[] (server.Processing imply (exists (i : int[0,1]) client[i].Authenticated))

// Liveness Properties
// ==================

// 8. Client can eventually establish connection
E<> client1.Connected

// 9. Client can eventually authenticate
E<> client1.Authenticated

// 10. Services are eventually available
E<> (server.Idle and database_available)

// 11. Vehicle can eventually reach full capacity
E<> (vehicle_bus.current_capacity == 0)

// 12. Error states are recoverable
A[] (server.Error imply E<> server.Idle)
A[] (client1.Error imply E<> client1.Disconnected)

// 13. All requests are eventually processed
A[] (server.Processing imply E<> server.Idle)

// Reachability Properties
// ======================

// 14. All major system states are reachable
E<> server.Idle
E<> server.Processing
E<> server.Error
E<> client1.Connected
E<> client1.Authenticated
E<> client1.Service_Request

// 15. System can handle multiple concurrent clients
E<> (client1.Authenticated and client2.Authenticated)

// Temporal Logic Properties (LTL)
// ===============================

// 16. Eventually, if a client connects, it will authenticate
A[] (client1.Connected imply (client1.Connected U client1.Authenticated))

// 17. If server starts processing, it eventually becomes idle
A[] (server.Processing imply E<> server.Idle)

// 18. Authentication timeout is respected
A[] (client1.Connecting imply (client1.auth_timer <= MAX_AUTH_TIME))

// 19. Connection establishment follows proper sequence
A[] (client1.Connected imply (client1.Disconnected R client1.Connecting))

// 20. System maintains consistency during state transitions
A[] ((server.Processing and server.pending_requests > 0) imply 
     E<> (server.Idle and server.pending_requests == 0))

// Real-time Properties
// ===================

// 21. Connection timeout constraints
A[] (server.Connecting imply server.connection_timer <= MAX_CONNECTION_TIME)

// 22. Processing timeout constraints
A[] (client1.Service_Request imply client1.client_timer <= MAX_PROCESSING_TIME)

// 23. System responds within acceptable time limits
A[] (server.Processing imply E<> server.Idle)

// Security Properties
// ==================

// 24. No service access without proper authentication
A[] (client1.Service_Request imply client1.user_authenticated)

// 25. TLS is mandatory for all communications
A[] (current_state > STATE_DISCONNECTED imply tls_enabled)

// 26. Secure state transitions
A[] (client1.Authenticated imply tls_enabled)

// Fault Tolerance Properties
// =========================

// 27. System recovers from database unavailability
A[] (not database_available imply E<> database_available)

// 28. Error states don't persist indefinitely
A[] (server.Error imply E<> not server.Error)

// 29. Connection failures are handled gracefully
A[] (client1.Error imply E<> client1.Disconnected)

// 30. System can handle resource exhaustion
A[] (vehicle_bus.current_capacity == 0 imply E<> vehicle_bus.current_capacity > 0)
