use std::net::{IpAddr, SocketAddr};
use std::sync::Arc;
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use axum::body::Body;
use axum::extract::rejection::JsonRejection;
use axum::extract::{ConnectInfo, DefaultBodyLimit, Multipart, OriginalUri, Path, Query, State};
use axum::http::header::{CONTENT_DISPOSITION, CONTENT_LENGTH, CONTENT_TYPE, LOCATION};
use axum::http::HeaderMap;
use axum::http::StatusCode;
use axum::response::{Html, IntoResponse, Response};
use axum::routing::{get, post, put};
use axum::{Json, Router};
use utoipa::OpenApi;

use crate::model::{
    DirectoryServerRecord, ListModsQuery, ListServersQuery, ModCatalogEntry, ModPackage,
    ModUsageServer, ObserveServerRequest, PruneServersRequest, PruneServersResponse,
    RegisterServerRequest, ServerDetail, ServerModReference, ServerPlayer, ServerPopulationSample,
    ServerRecentSession, ServerVersionGroup, ServiceMetadata, ServiceSummary,
    SetModCompatibilityRequest,
};
use crate::mods::ModUploadMeta;
use crate::repository::{SqliteServerDirectory, TokenRelocation};
use crate::service::{self, PapaBearService};

/// A silent (no-heartbeat) row older than this may be reclaimed without the token, so a
/// crashed server that lost its token recovers; an actively-heartbeating row is protected.
const SERVER_TOKEN_RECOVERY_MS: i64 = 120_000;

/// Default upper bound on a mod upload body. Whole-`@Mod` wrappers are uncompressed
/// (store-only) and routinely run to many GB (e.g. RCWC ~11GB). The artifact is streamed
/// to the store (not buffered), so this is a sanity cap, not a memory bound. Override at
/// runtime with `PAPABEAR_MAX_MOD_UPLOAD_BYTES` (bytes).
const DEFAULT_MAX_MOD_UPLOAD_BYTES: usize = 32 * 1024 * 1024 * 1024;

fn max_mod_upload_bytes() -> usize {
    std::env::var("PAPABEAR_MAX_MOD_UPLOAD_BYTES")
        .ok()
        .and_then(|v| v.parse::<usize>().ok())
        .filter(|&v| v > 0)
        .unwrap_or(DEFAULT_MAX_MOD_UPLOAD_BYTES)
}

#[derive(OpenApi)]
#[openapi(
    info(
        title = "PAPA BEAR",
        version = "v1",
        description = "PAPA BEAR is the OFP game-service foundation for server discovery and future browser, observer, and mod distribution features."
    ),
    paths(
        list_servers,
        list_server_versions,
        get_server,
        list_mods,
        get_mod,
        list_mod_revisions,
        get_mod_revision,
        set_mod_revision_compatibility,
        download_mod_revision,
        list_mod_versions,
        list_mod_servers,
        register_server,
        heartbeat_server_doc,
        observe_server,
        prune_servers,
        unregister_server,
        service_metadata,
        service_summary,
        openapi_spec
    ),
    components(schemas(
        RegisterServerRequest,
        DirectoryServerRecord,
        ServerVersionGroup,
        ObserveServerRequest,
        ServerDetail,
        ServerModReference,
        ServerPlayer,
        ServerPopulationSample,
        ServerRecentSession,
        ModUsageServer,
        ModCatalogEntry,
        ModPackage,
        SetModCompatibilityRequest,
        PruneServersRequest,
        PruneServersResponse,
        ServiceMetadata,
        ServiceSummary
    ))
)]
struct ApiDoc;

#[derive(Clone)]
struct AppState {
    service: Arc<PapaBearService>,
    admin_api_key: Option<Arc<str>>,
    /// Trusted header carrying the real client IP (e.g. `X-Forwarded-For` behind an ingress).
    /// When set, public entries bind the server's address+id to it instead of the body.
    client_ip_header: Option<Arc<str>>,
    /// When true, heartbeat/unregister of an active row require the matching server token.
    /// A token is always issued on register regardless, so this can be flipped on once
    /// publishers send it.
    require_server_token: bool,
}

pub fn openapi_document() -> utoipa::openapi::OpenApi {
    ApiDoc::openapi()
}

pub fn openapi_yaml() -> Result<String, String> {
    openapi_document()
        .to_yaml()
        .map_err(|error| error.to_string())
}

pub fn build_router(directory: Arc<SqliteServerDirectory>) -> Router {
    build_router_with_options(directory, None, None, None, false)
}

pub fn build_router_with_admin_key(
    directory: Arc<SqliteServerDirectory>,
    admin_api_key: Option<String>,
) -> Router {
    build_router_with_options(directory, admin_api_key, None, None, false)
}

pub fn build_router_with_options(
    directory: Arc<SqliteServerDirectory>,
    admin_api_key: Option<String>,
    mods_root: Option<std::path::PathBuf>,
    client_ip_header: Option<String>,
    require_server_token: bool,
) -> Router {
    let service = Arc::new(PapaBearService::with_mods_root(directory, mods_root));
    build_router_for_service(
        service,
        admin_api_key,
        client_ip_header,
        require_server_token,
    )
}

/// Build the router around a pre-constructed mod store (the deploy path uses an S3/MinIO
/// store; tests and local dev go through `build_router_with_options`).
pub fn build_router_with_store(
    directory: Arc<SqliteServerDirectory>,
    admin_api_key: Option<String>,
    mod_store: Option<crate::mods::ModStore>,
    client_ip_header: Option<String>,
    require_server_token: bool,
) -> Router {
    let service = Arc::new(PapaBearService::with_mod_store(directory, mod_store));
    build_router_for_service(
        service,
        admin_api_key,
        client_ip_header,
        require_server_token,
    )
}

fn build_router_for_service(
    service: Arc<PapaBearService>,
    admin_api_key: Option<String>,
    client_ip_header: Option<String>,
    require_server_token: bool,
) -> Router {
    Router::new()
        .route("/healthz", get(healthz))
        .route("/", get(landing_page))
        .route("/browser", get(browser_page))
        .route("/browser/:server_id", get(browser_detail_page))
        .route("/mods", get(mods_page))
        .route("/mods/:mod_id", get(mod_detail_page))
        .route("/assets/site.css", get(site_css))
        .route("/assets/papa-bear.js", get(site_javascript))
        .route("/v1/servers", get(list_servers))
        .route("/v1/servers/versions", get(list_server_versions))
        .route(
            "/v1/mods",
            get(list_mods)
                .post(publish_mod)
                .layer(DefaultBodyLimit::max(max_mod_upload_bytes())),
        )
        .route("/v1/mods/:mod_id", get(get_mod).delete(delete_mod))
        .route(
            "/v1/mods/:mod_id/revisions",
            get(list_mod_revisions)
                .post(publish_revision)
                .layer(DefaultBodyLimit::max(max_mod_upload_bytes())),
        )
        .route(
            "/v1/mods/:mod_id/revisions/:revision",
            get(get_mod_revision),
        )
        .route(
            "/v1/mods/:mod_id/revisions/:revision/compatibility",
            put(set_mod_revision_compatibility),
        )
        .route(
            "/v1/mods/:mod_id/revisions/:revision/download",
            get(download_mod_revision),
        )
        .route("/v1/mods/:mod_id/versions", get(list_mod_versions))
        .route("/v1/mods/:mod_id/servers", get(list_mod_servers))
        .route("/v1/mods/:mod_id/download", get(download_mod))
        .route("/v1/servers/register", post(register_server))
        .route("/v1/servers/heartbeat", post(register_server))
        .route("/v1/servers/prune", post(prune_servers))
        .route("/v1/servers/:server_id/observe", post(observe_server))
        .route(
            "/v1/servers/:server_id",
            get(get_server).delete(unregister_server),
        )
        .route("/v1/meta/service", get(service_metadata))
        .route("/v1/meta/summary", get(service_summary))
        .route("/openapi/v1.yaml", get(openapi_spec))
        .with_state(AppState {
            service,
            admin_api_key: admin_api_key.map(Arc::<str>::from),
            client_ip_header: client_ip_header.map(Arc::<str>::from),
            require_server_token,
        })
}

#[utoipa::path(
    post,
    path = "/v1/servers/register",
    request_body = RegisterServerRequest,
    responses(
        (status = 200, description = "Registered server row", body = DirectoryServerRecord)
    )
)]
async fn register_server(
    State(state): State<AppState>,
    OriginalUri(uri): OriginalUri,
    headers: HeaderMap,
    connect_info: Option<ConnectInfo<SocketAddr>>,
    payload: Result<Json<RegisterServerRequest>, JsonRejection>,
) -> Result<Json<DirectoryServerRecord>, (StatusCode, String)> {
    let Json(mut request) = payload.map_err(|rejection| {
        let status = rejection.status();
        publisher_error(uri.path(), &headers, status, rejection.body_text())
    })?;
    let provided_token = bearer_token(&headers);
    if provided_token
        .as_deref()
        .is_some_and(|token| !valid_server_token(token))
    {
        return Err(publisher_error(
            uri.path(),
            &headers,
            StatusCode::UNAUTHORIZED,
            "invalid server token",
        ));
    }
    let provided_hash = provided_token.as_deref().map(service::token_hash);
    let published_ip =
        publish_client_ip(&state, &headers, connect_info.as_ref().map(|info| info.0));
    if let Some(ip) = published_ip {
        request.server_id = format!("{ip}:{}", request.hostport);
        request.address = ip;
    } else if state.client_ip_header.is_some() {
        let owner = match provided_hash.as_deref() {
            Some(hash) => state
                .service
                .server_id_for_token_hash(hash)
                .await
                .map_err(|error| internal_error(&error))?,
            None => None,
        };
        if let Some(owner) = owner {
            let record = state
                .service
                .get(&owner)
                .await
                .map_err(|error| internal_error(&error))?
                .ok_or_else(|| internal_error(&anyhow::anyhow!("token owner disappeared")))?;
            request.server_id = record.server_id;
            request.address = record.address;
            request.hostport = record.hostport;
        } else if is_public_forwarded_ip(&request.address) {
            request.server_id = format!("{}:{}", request.address, request.hostport);
        } else {
            return Err(publisher_error(
                uri.path(),
                &headers,
                StatusCode::SERVICE_UNAVAILABLE,
                "no public publisher address is available",
            ));
        }
    }
    let server_id = request.server_id.clone();
    let now_unix_ms = current_time_millis();

    let token_owner_found = if let Some(hash) = provided_hash.as_deref() {
        match state
            .service
            .relocate_token_owner(
                hash,
                &request.server_id,
                &request.address,
                request.hostport,
                now_unix_ms,
                SERVER_TOKEN_RECOVERY_MS,
            )
            .await
            .map_err(|error| internal_error(&error))?
        {
            TokenRelocation::NotFound => false,
            TokenRelocation::Unchanged => true,
            TokenRelocation::Moved => {
                tracing::info!(
                    route = %uri.path(),
                    server_id = %request.server_id,
                    address = %request.address,
                    hostport = request.hostport,
                    "Master server publisher endpoint relocated"
                );
                true
            }
            TokenRelocation::Conflict => {
                return Err(publisher_error(
                    uri.path(),
                    &headers,
                    StatusCode::CONFLICT,
                    "publisher endpoint is owned by another token",
                ));
            }
        }
    } else {
        false
    };

    let existing = state
        .service
        .get(&server_id)
        .await
        .map_err(|error| internal_error(&error))?;
    let stored_hash = state
        .service
        .server_token_hash(&server_id)
        .await
        .map_err(|error| internal_error(&error))?;
    let token_ok = token_owner_found
        || matches!(
            (&provided_token, &stored_hash),
            (Some(token), Some(hash)) if &service::token_hash(token) == hash
        );

    // Protect an actively-heartbeating, token-held row from takeover by a wrong/missing
    // token (only when enforcement is on). A stale row (silent owner) may be reclaimed,
    // which is the crash-recovery path.
    if state.require_server_token && !token_ok && stored_hash.is_some() {
        let fresh = existing
            .is_some_and(|row| now_unix_ms - row.last_seen_unix_ms < SERVER_TOKEN_RECOVERY_MS);
        if fresh {
            return Err(publisher_error(
                uri.path(),
                &headers,
                StatusCode::UNAUTHORIZED,
                "missing or invalid server token",
            ));
        }
    }

    if token_ok {
        let record = state
            .service
            .register(request, now_unix_ms)
            .await
            .map_err(|error| internal_error(&error))?;
        return Ok(Json(record));
    }
    let (claim_hash, issued_token) = if let Some(hash) = provided_hash {
        (hash, None)
    } else {
        let (token, hash) = service::issue_token().map_err(|error| internal_error(&error))?;
        (hash, Some(token))
    };
    let mut record = state
        .service
        .register_with_token_claim(request, now_unix_ms, &claim_hash, stored_hash.as_deref())
        .await
        .map_err(|error| internal_error(&error))?
        .ok_or_else(|| {
            publisher_error(
                uri.path(),
                &headers,
                StatusCode::CONFLICT,
                "publisher endpoint ownership changed during registration",
            )
        })?;
    record.token = issued_token;
    Ok(Json(record))
}

#[utoipa::path(
    get,
    path = "/v1/servers",
    params(ListServersQuery),
    responses(
        (status = 200, description = "Server rows", body = [DirectoryServerRecord])
    )
)]
async fn list_servers(
    State(state): State<AppState>,
    Query(query): Query<ListServersQuery>,
) -> Result<Json<Vec<DirectoryServerRecord>>, (StatusCode, String)> {
    let records = state
        .service
        .list(&query)
        .await
        .map_err(|error| internal_error(&error))?;
    Ok(Json(records))
}

#[utoipa::path(
    get,
    path = "/v1/servers/versions",
    params(ListServersQuery),
    responses(
        (status = 200, description = "Live server app/version groups", body = [ServerVersionGroup])
    )
)]
async fn list_server_versions(
    State(state): State<AppState>,
    Query(mut query): Query<ListServersQuery>,
) -> Result<Json<Vec<ServerVersionGroup>>, (StatusCode, String)> {
    query.app_name = None;
    query.actver = None;
    query.version_tag = None;
    let records = state
        .service
        .version_groups(&query)
        .await
        .map_err(|error| internal_error(&error))?;
    Ok(Json(records))
}

#[utoipa::path(
    get,
    path = "/v1/servers/{serverId}",
    params(
        ("serverId" = String, Path, description = "Server identifier")
    ),
    responses(
        (status = 200, description = "Server detail", body = ServerDetail),
        (status = 404, description = "Server not found")
    )
)]
async fn get_server(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(server_id): Path<String>,
) -> Result<Json<ServerDetail>, StatusCode> {
    match state
        .service
        .get_server_detail(&server_id)
        .await
        .map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?
    {
        Some(mut detail) => {
            let base = request_base_url(&headers);
            for mod_ref in &mut detail.mods {
                absolutize_download_url(&mut mod_ref.download_url, base.as_deref());
            }
            Ok(Json(detail))
        }
        None => Err(StatusCode::NOT_FOUND),
    }
}

#[utoipa::path(
    get,
    path = "/v1/mods",
    params(ListModsQuery),
    responses(
        (status = 200, description = "Mod catalog rows", body = [ModCatalogEntry])
    )
)]
async fn list_mods(
    State(state): State<AppState>,
    headers: HeaderMap,
    Query(query): Query<ListModsQuery>,
) -> Result<Json<Vec<ModCatalogEntry>>, (StatusCode, String)> {
    let mut records = state
        .service
        .list_mods(&query)
        .await
        .map_err(|error| internal_error(&error))?;
    let base = request_base_url(&headers);
    for entry in &mut records {
        absolutize_download_url(&mut entry.download_url, base.as_deref());
    }
    Ok(Json(records))
}

#[utoipa::path(
    get,
    path = "/v1/mods/{modId}",
    params(
        ("modId" = String, Path, description = "Mod identifier")
    ),
    responses(
        (status = 200, description = "Mod metadata", body = ModCatalogEntry),
        (status = 404, description = "Mod not found")
    )
)]
async fn get_mod(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(mod_id): Path<String>,
) -> Result<Json<ModCatalogEntry>, StatusCode> {
    match state
        .service
        .get_mod(&mod_id)
        .await
        .map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?
    {
        Some(mut entry) => {
            absolutize_download_url(
                &mut entry.download_url,
                request_base_url(&headers).as_deref(),
            );
            Ok(Json(entry))
        }
        None => Err(StatusCode::NOT_FOUND),
    }
}

#[utoipa::path(
    get,
    path = "/v1/mods/{modId}/versions",
    params(
        ("modId" = String, Path, description = "Mod identifier")
    ),
    responses(
        (status = 200, description = "Related mod catalog versions", body = [ModCatalogEntry])
    )
)]
async fn list_mod_versions(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(mod_id): Path<String>,
) -> Result<Json<Vec<ModCatalogEntry>>, (StatusCode, String)> {
    let mut records = state
        .service
        .mod_versions(&mod_id)
        .await
        .map_err(|error| internal_error(&error))?;
    let base = request_base_url(&headers);
    for entry in &mut records {
        absolutize_download_url(&mut entry.download_url, base.as_deref());
    }
    Ok(Json(records))
}

#[utoipa::path(
    get,
    path = "/v1/mods/{modId}/revisions",
    params(("modId" = String, Path, description = "Stable mod identifier")),
    responses(
        (status = 200, description = "Immutable package revisions, newest first", body = [ModCatalogEntry])
    )
)]
async fn list_mod_revisions(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(mod_id): Path<String>,
) -> Result<Json<Vec<ModCatalogEntry>>, (StatusCode, String)> {
    let mut records = state
        .service
        .mod_revisions(&mod_id)
        .await
        .map_err(|error| internal_error(&error))?;
    let base = request_base_url(&headers);
    for entry in &mut records {
        absolutize_download_url(&mut entry.download_url, base.as_deref());
    }
    Ok(Json(records))
}

#[utoipa::path(
    get,
    path = "/v1/mods/{modId}/revisions/{revision}",
    params(
        ("modId" = String, Path, description = "Stable mod identifier"),
        ("revision" = u64, Path, description = "Package revision")
    ),
    responses(
        (status = 200, description = "Immutable package revision metadata", body = ModCatalogEntry),
        (status = 404, description = "Revision not found")
    )
)]
async fn get_mod_revision(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path((mod_id, revision)): Path<(String, u64)>,
) -> Result<Json<ModCatalogEntry>, StatusCode> {
    match state
        .service
        .get_mod_revision(&mod_id, revision)
        .await
        .map_err(|_| StatusCode::INTERNAL_SERVER_ERROR)?
    {
        Some(mut entry) => {
            absolutize_download_url(
                &mut entry.download_url,
                request_base_url(&headers).as_deref(),
            );
            Ok(Json(entry))
        }
        None => Err(StatusCode::NOT_FOUND),
    }
}

#[utoipa::path(
    put,
    path = "/v1/mods/{modId}/revisions/{revision}/compatibility",
    request_body = SetModCompatibilityRequest,
    params(
        ("modId" = String, Path, description = "Stable mod identifier"),
        ("revision" = u64, Path, description = "Package revision")
    ),
    responses(
        (status = 200, description = "Updated revision compatibility", body = ModCatalogEntry),
        (status = 401, description = "Missing or invalid administrator key"),
        (status = 404, description = "Revision not found")
    )
)]
async fn set_mod_revision_compatibility(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path((mod_id, revision)): Path<(String, u64)>,
    payload: Result<Json<SetModCompatibilityRequest>, JsonRejection>,
) -> Result<Json<ModCatalogEntry>, (StatusCode, String)> {
    require_admin_api_key(&state, &headers)?;
    let Json(request) = payload.map_err(|rejection| (rejection.status(), rejection.body_text()))?;
    let entry = state
        .service
        .set_mod_revision_compatibility(&mod_id, revision, &request.compatible_actvers)
        .await
        .map_err(|error| (StatusCode::BAD_REQUEST, error.to_string()))?;
    entry
        .map(Json)
        .ok_or_else(|| (StatusCode::NOT_FOUND, "revision not found".to_string()))
}

#[utoipa::path(
    get,
    path = "/v1/mods/{modId}/servers",
    params(
        ("modId" = String, Path, description = "Mod identifier")
    ),
    responses(
        (status = 200, description = "Servers currently using this mod", body = [ModUsageServer])
    )
)]
async fn list_mod_servers(
    State(state): State<AppState>,
    Path(mod_id): Path<String>,
) -> Result<Json<Vec<ModUsageServer>>, (StatusCode, String)> {
    let records = state
        .service
        .mod_usage_servers(&mod_id)
        .await
        .map_err(|error| internal_error(&error))?;
    Ok(Json(records))
}

/// Publish a packed mod to the workshop. Admin-gated (`x-api-key`); accepts a
/// `multipart/form-data` body with a `name` field, optional `app`/`actver`/`vertag`,
/// optional `version`/`description`/`homepageUrl`, repeated `author` fields, and the PBO
/// as the `file` field.
async fn publish_mod(
    State(state): State<AppState>,
    headers: HeaderMap,
    multipart: Multipart,
) -> Result<Json<ModCatalogEntry>, (StatusCode, String)> {
    upload_mod(state, headers, multipart, None).await
}

async fn publish_revision(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(mod_id): Path<String>,
    multipart: Multipart,
) -> Result<Json<ModCatalogEntry>, (StatusCode, String)> {
    upload_mod(state, headers, multipart, Some(mod_id)).await
}

async fn upload_mod(
    state: AppState,
    headers: HeaderMap,
    mut multipart: Multipart,
    target_mod_id: Option<String>,
) -> Result<Json<ModCatalogEntry>, (StatusCode, String)> {
    require_admin_api_key(&state, &headers)?;
    if !state.service.mods_enabled() {
        return Err((
            StatusCode::SERVICE_UNAVAILABLE,
            "mods directory is not configured".to_string(),
        ));
    }

    let Some(mut upload) = state
        .service
        .begin_mod_upload()
        .await
        .map_err(|error| internal_error(&error))?
    else {
        return Err((
            StatusCode::SERVICE_UNAVAILABLE,
            "mods directory is not configured".to_string(),
        ));
    };

    let mut name: Option<String> = None;
    let mut mod_id: Option<String> = None;
    let mut app_name: Option<String> = None;
    let mut actver: Option<String> = None;
    let mut compatible_actvers: Vec<String> = Vec::new();
    let mut version_tag: Option<String> = None;
    let mut version: Option<String> = None;
    let mut folder_name: Option<String> = None;
    let mut description: Option<String> = None;
    let mut homepage_url: Option<String> = None;
    let mut authors: Vec<String> = Vec::new();
    let mut file_seen = false;

    while let Some(mut field) = multipart
        .next_field()
        .await
        .map_err(|error| (StatusCode::BAD_REQUEST, error.to_string()))?
    {
        let field_name = field.name().map(str::to_string);
        match field_name.as_deref() {
            Some("name") => name = Some(read_text_field(field).await?),
            Some("modId") => mod_id = Some(read_text_field(field).await?),
            Some("app") => app_name = Some(read_text_field(field).await?),
            Some("actver") => actver = Some(read_text_field(field).await?),
            Some("compatibleActver") => compatible_actvers.push(read_text_field(field).await?),
            Some("vertag") => version_tag = Some(read_text_field(field).await?),
            Some("version") => version = Some(read_text_field(field).await?),
            Some("folderName") => folder_name = Some(read_text_field(field).await?),
            Some("description") => description = Some(read_text_field(field).await?),
            Some("homepageUrl") => homepage_url = Some(read_text_field(field).await?),
            Some("author") => authors.push(read_text_field(field).await?),
            Some("file") => {
                // Stream the artifact straight to the store — never buffer the whole file.
                file_seen = true;
                while let Some(chunk) = field
                    .chunk()
                    .await
                    .map_err(|error| (StatusCode::BAD_REQUEST, error.to_string()))?
                {
                    upload.write(&chunk);
                }
            }
            _ => {
                // Drain unknown fields so the stream stays in sync.
                while field.chunk().await.ok().flatten().is_some() {}
            }
        }
    }

    let name = name.filter(|value| !value.trim().is_empty());
    if (target_mod_id.is_none() && name.is_none()) || !file_seen {
        // Clean up the temp upload, then report the missing field.
        let _ = state
            .service
            .finalize_mod_upload(upload, ModUploadMeta::default())
            .await;
        let missing = if !file_seen { "file" } else { "name" };
        return Err((
            StatusCode::BAD_REQUEST,
            format!("missing '{missing}' field"),
        ));
    }

    let compatible_actvers = compatible_actvers
        .into_iter()
        .map(|value| {
            value
                .trim()
                .parse::<i32>()
                .ok()
                .filter(|value| *value > 0)
                .ok_or_else(|| {
                    (
                        StatusCode::BAD_REQUEST,
                        format!("invalid compatibleActver '{value}'"),
                    )
                })
        })
        .collect::<Result<Vec<_>, _>>()?;
    let meta = ModUploadMeta {
        mod_id: mod_id.filter(|value| !value.trim().is_empty()),
        name: name.unwrap_or_default(),
        app_name: app_name.filter(|value| !value.trim().is_empty()),
        actver: actver.and_then(|value| value.trim().parse().ok()),
        compatible_actvers,
        version_tag: version_tag.filter(|value| !value.trim().is_empty()),
        version: version.filter(|value| !value.trim().is_empty()),
        folder_name: folder_name.filter(|value| !value.trim().is_empty()),
        description: description.filter(|value| !value.is_empty()),
        authors,
        homepage_url: homepage_url.filter(|value| !value.trim().is_empty()),
    };
    let result = match target_mod_id {
        Some(mod_id) => {
            state
                .service
                .finalize_mod_revision_upload(upload, &mod_id, meta)
                .await
        }
        None => state.service.finalize_mod_upload(upload, meta).await,
    };
    result.map(Json).map_err(|error| {
        let message = error.to_string();
        let status = if message.contains("already exists") {
            StatusCode::CONFLICT
        } else if message.contains("does not exist") {
            StatusCode::NOT_FOUND
        } else {
            StatusCode::BAD_REQUEST
        };
        (status, message)
    })
}

/// Delete a published mod (admin-gated): removes its catalog entry and its artifact from
/// the store. 204 on success, 404 if the mod doesn't exist, 401 without a valid admin key.
async fn delete_mod(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(mod_id): Path<String>,
) -> Result<StatusCode, (StatusCode, String)> {
    require_admin_api_key(&state, &headers)?;
    if !state.service.mods_enabled() {
        return Err((
            StatusCode::SERVICE_UNAVAILABLE,
            "mods directory is not configured".to_string(),
        ));
    }
    let deleted = state
        .service
        .delete_mod(&mod_id)
        .await
        .map_err(|error| internal_error(&error))?;
    if deleted {
        Ok(StatusCode::NO_CONTENT)
    } else {
        Err((StatusCode::NOT_FOUND, "mod not found".to_string()))
    }
}

async fn download_mod(State(state): State<AppState>, Path(mod_id): Path<String>) -> Response {
    match state.service.get_mod(&mod_id).await {
        Ok(Some(entry)) => (
            [(
                LOCATION,
                format!(
                    "/v1/mods/{mod_id}/revisions/{}/download",
                    entry.package_revision
                ),
            )],
            StatusCode::TEMPORARY_REDIRECT,
        )
            .into_response(),
        Ok(None) => StatusCode::NOT_FOUND.into_response(),
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}

#[utoipa::path(
    get,
    path = "/v1/mods/{modId}/revisions/{revision}/download",
    params(
        ("modId" = String, Path, description = "Stable mod identifier"),
        ("revision" = u64, Path, description = "Package revision")
    ),
    responses(
        (status = 200, description = "Immutable compressed package artifact"),
        (status = 307, description = "Redirect to immutable object-store URL"),
        (status = 404, description = "Revision not found")
    )
)]
async fn download_mod_revision(
    State(state): State<AppState>,
    Path((mod_id, revision)): Path<(String, u64)>,
) -> Response {
    match state
        .service
        .mod_revision_artifact_signed_url(&mod_id, revision)
        .await
    {
        Ok(Some((_size, url))) => {
            return ([(LOCATION, url)], StatusCode::TEMPORARY_REDIRECT).into_response();
        }
        Ok(None) => {}
        Err(_) => return StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }

    match state
        .service
        .mod_revision_artifact_stream(&mod_id, revision)
        .await
    {
        Ok(Some((size, stream))) => {
            // Stream the artifact straight from the store — never buffer the whole file.
            let headers = [
                (CONTENT_TYPE, "application/octet-stream".to_string()),
                (
                    CONTENT_DISPOSITION,
                    format!("attachment; filename=\"{mod_id}.pbo\""),
                ),
                (CONTENT_LENGTH, size.to_string()),
            ];
            (headers, Body::from_stream(stream)).into_response()
        }
        Ok(None) => StatusCode::NOT_FOUND.into_response(),
        Err(_) => StatusCode::INTERNAL_SERVER_ERROR.into_response(),
    }
}

async fn read_text_field(
    field: axum::extract::multipart::Field<'_>,
) -> Result<String, (StatusCode, String)> {
    field
        .text()
        .await
        .map_err(|error| (StatusCode::BAD_REQUEST, error.to_string()))
}

#[utoipa::path(
    delete,
    path = "/v1/servers/{serverId}",
    params(
        ("serverId" = String, Path, description = "Server identifier")
    ),
    responses(
        (status = 204, description = "Server removed"),
        (status = 404, description = "Server not found")
    )
)]
async fn unregister_server(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(server_id): Path<String>,
) -> Result<StatusCode, (StatusCode, String)> {
    let provided = bearer_token(&headers);
    let authenticated_server_id = if let Some(token) = provided.as_deref() {
        if !valid_server_token(token) {
            return Err((StatusCode::UNAUTHORIZED, "invalid server token".to_string()));
        }
        state
            .service
            .server_id_for_token_hash(&service::token_hash(token))
            .await
            .map_err(|error| internal_error(&error))?
    } else {
        None
    };
    let delete_server_id = if let Some(owner) = authenticated_server_id {
        owner
    } else {
        if let Some(ip) = caller_client_ip(&state, &headers) {
            let owner_ip = server_id
                .rsplit_once(':')
                .map_or(server_id.as_str(), |(host, _)| host);
            if owner_ip != ip {
                return Err((
                    StatusCode::FORBIDDEN,
                    "server id does not match caller address".to_string(),
                ));
            }
        }
        if state.require_server_token {
            let stored = state
                .service
                .server_token_hash(&server_id)
                .await
                .map_err(|error| internal_error(&error))?;
            if stored.is_some() {
                let token_ok = matches!((&provided, &stored), (Some(token), Some(hash)) if &service::token_hash(token) == hash);
                if !token_ok {
                    return Err((
                        StatusCode::UNAUTHORIZED,
                        "missing or invalid server token".to_string(),
                    ));
                }
            }
        }
        server_id
    };
    if state
        .service
        .unregister(&delete_server_id)
        .await
        .map_err(|error| internal_error(&error))?
    {
        Ok(StatusCode::NO_CONTENT)
    } else {
        Ok(StatusCode::NOT_FOUND)
    }
}

#[utoipa::path(
    post,
    path = "/v1/servers/{serverId}/observe",
    params(
        ("serverId" = String, Path, description = "Server identifier"),
        ("x-api-key" = Option<String>, Header, description = "Admin API key when protected admin access is enabled")
    ),
    request_body = ObserveServerRequest,
    responses(
        (status = 200, description = "Updated server observation state", body = DirectoryServerRecord),
        (status = 401, description = "Missing or invalid admin API key"),
        (status = 404, description = "Server not found")
    )
)]
async fn observe_server(
    State(state): State<AppState>,
    headers: HeaderMap,
    Path(server_id): Path<String>,
    Json(request): Json<ObserveServerRequest>,
) -> Result<Json<DirectoryServerRecord>, (StatusCode, String)> {
    require_admin_api_key(&state, &headers)?;
    let observed_unix_ms = request.observed_unix_ms.unwrap_or_else(current_time_millis);
    state
        .service
        .observe(&server_id, observed_unix_ms, request.reachable)
        .await
        .map_err(|error| internal_error(&error))?
        .map_or_else(
            || {
                Err((
                    StatusCode::NOT_FOUND,
                    format!("server {server_id} not found"),
                ))
            },
            |record| Ok(Json(record)),
        )
}

#[utoipa::path(
    post,
    path = "/v1/servers/prune",
    params(
        ("x-api-key" = Option<String>, Header, description = "Admin API key when protected admin access is enabled")
    ),
    request_body = PruneServersRequest,
    responses(
        (status = 200, description = "Prune result", body = PruneServersResponse),
        (status = 401, description = "Missing or invalid admin API key")
    )
)]
async fn prune_servers(
    State(state): State<AppState>,
    headers: HeaderMap,
    Json(request): Json<PruneServersRequest>,
) -> Result<Json<PruneServersResponse>, (StatusCode, String)> {
    require_admin_api_key(&state, &headers)?;
    let removed = state
        .service
        .prune_stale(current_time_millis() - request.max_age_ms)
        .await
        .map_err(|error| internal_error(&error))?;
    Ok(Json(PruneServersResponse { removed }))
}

#[utoipa::path(
    get,
    path = "/v1/meta/service",
    responses(
        (status = 200, description = "Service metadata", body = ServiceMetadata)
    )
)]
async fn service_metadata(
    State(state): State<AppState>,
) -> Result<Json<ServiceMetadata>, (StatusCode, String)> {
    Ok(Json(state.service.metadata()))
}

#[utoipa::path(
    get,
    path = "/v1/meta/summary",
    responses(
        (status = 200, description = "Public landing-page summary", body = ServiceSummary)
    )
)]
async fn service_summary(
    State(state): State<AppState>,
) -> Result<Json<ServiceSummary>, (StatusCode, String)> {
    let summary = state
        .service
        .summary()
        .await
        .map_err(|error| internal_error(&error))?;
    Ok(Json(summary))
}

#[utoipa::path(
    get,
    path = "/openapi/v1.yaml",
    responses(
        (status = 200, description = "OpenAPI YAML", content_type = "application/yaml")
    )
)]
async fn openapi_spec() -> Result<impl IntoResponse, (StatusCode, String)> {
    let yaml = openapi_yaml().map_err(|error| (StatusCode::INTERNAL_SERVER_ERROR, error))?;
    Ok(([(CONTENT_TYPE, "application/yaml; charset=utf-8")], yaml))
}

#[derive(serde::Serialize)]
struct HealthStatus {
    status: &'static str,
}

// Liveness probe. Stays dependency-free: the SQLite directory is opened at startup, so a
// runtime liveness check must not fail on transient per-request DB errors.
async fn healthz() -> impl IntoResponse {
    Json(HealthStatus { status: "ok" })
}

async fn landing_page() -> Html<&'static str> {
    Html(include_str!("../web/index.html"))
}

async fn browser_page() -> Html<&'static str> {
    Html(include_str!("../web/browser.html"))
}

async fn browser_detail_page() -> Html<&'static str> {
    Html(include_str!("../web/browser-detail.html"))
}

async fn mods_page() -> Html<&'static str> {
    Html(include_str!("../web/mods.html"))
}

async fn mod_detail_page() -> Html<&'static str> {
    Html(include_str!("../web/mod-detail.html"))
}

async fn site_css() -> impl IntoResponse {
    (
        [(CONTENT_TYPE, "text/css; charset=utf-8")],
        include_str!("../web/assets/site.css"),
    )
}

async fn site_javascript() -> impl IntoResponse {
    (
        [(CONTENT_TYPE, "application/javascript; charset=utf-8")],
        include_str!("../web/assets/papa-bear.js"),
    )
}

fn current_time_millis() -> i64 {
    let duration = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_else(|_| Duration::default());
    i64::try_from(duration.as_millis()).unwrap_or(i64::MAX)
}

fn internal_error(error: &anyhow::Error) -> (StatusCode, String) {
    (StatusCode::INTERNAL_SERVER_ERROR, error.to_string())
}

fn publisher_error(
    route: &str,
    headers: &HeaderMap,
    status: StatusCode,
    reason: impl Into<String>,
) -> (StatusCode, String) {
    let reason = reason.into();
    tracing::warn!(
        route,
        status = status.as_u16(),
        request_id = headers
            .get("x-request-id")
            .and_then(|value| value.to_str().ok())
            .unwrap_or("-"),
        user_agent = headers
            .get("user-agent")
            .and_then(|value| value.to_str().ok())
            .unwrap_or("-"),
        token_present = bearer_token(headers).is_some(),
        reason,
        "Master server publisher request rejected"
    );
    (status, reason)
}

/// Public base URL (`scheme://host`) for absolutising catalog links, derived from the
/// request: the `Host` header plus `X-Forwarded-Proto` (set by the TLS-terminating
/// proxy; defaults to `http` for a direct/local connection). `None` when there is no
/// usable Host header — the link is then left as stored (relative).
fn request_base_url(headers: &HeaderMap) -> Option<String> {
    let host = headers
        .get(axum::http::header::HOST)
        .and_then(|value| value.to_str().ok())
        .map(str::trim)
        .filter(|value| !value.is_empty())?;
    let scheme = headers
        .get("x-forwarded-proto")
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.split(',').next())
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .unwrap_or("http");
    Some(format!("{scheme}://{host}"))
}

/// Rewrite a server-relative `download_url` (`/v1/mods/<id>/download`) to an absolute URL
/// against `base`, so clients (libcurl) get a usable URL. An already-absolute URL (a CDN
/// link) and `None` are left untouched, and the stored manifest stays host-portable.
fn absolutize_download_url(url: &mut Option<String>, base: Option<&str>) {
    let (Some(current), Some(base)) = (url.as_deref(), base) else {
        return;
    };
    if current.is_empty() || current.contains("://") {
        return;
    }
    let base = base.trim_end_matches('/');
    let path = if current.starts_with('/') {
        current.to_string()
    } else {
        format!("/{current}")
    };
    *url = Some(format!("{base}{path}"));
}

fn forwarded_ips(state: &AppState, headers: &HeaderMap) -> Option<Vec<String>> {
    let header = state.client_ip_header.as_ref()?;
    headers
        .get(header.as_ref())
        .and_then(|value| value.to_str().ok())
        .map(|value| {
            value
                .split(',')
                .map(str::trim)
                .filter(|value| !value.is_empty())
                .map(str::to_string)
                .collect::<Vec<_>>()
        })
        .filter(|values| !values.is_empty())
}

fn publish_client_ip(
    state: &AppState,
    headers: &HeaderMap,
    peer_addr: Option<SocketAddr>,
) -> Option<String> {
    forwarded_ips(state, headers)
        .into_iter()
        .flatten()
        .find(|value| is_public_forwarded_ip(value))
        .or_else(|| {
            peer_addr
                .map(|addr| addr.ip().to_string())
                .filter(|value| is_public_forwarded_ip(value))
        })
}

fn caller_client_ip(state: &AppState, headers: &HeaderMap) -> Option<String> {
    let ips = forwarded_ips(state, headers)?;
    ips.iter()
        .find(|value| is_public_forwarded_ip(value))
        .cloned()
        .or_else(|| ips.into_iter().next())
}

fn is_public_forwarded_ip(value: &str) -> bool {
    match value.parse::<IpAddr>() {
        Ok(IpAddr::V4(ip)) => {
            !(ip.is_private()
                || ip.is_loopback()
                || ip.is_link_local()
                || ip.is_unspecified()
                || ip.is_multicast())
        }
        Ok(IpAddr::V6(ip)) => {
            !(ip.is_loopback()
                || ip.is_unspecified()
                || ip.is_multicast()
                || is_ipv6_unique_local(ip)
                || is_ipv6_link_local(ip))
        }
        Err(_) => false,
    }
}

fn is_ipv6_unique_local(ip: std::net::Ipv6Addr) -> bool {
    (ip.segments()[0] & 0xfe00) == 0xfc00
}

fn is_ipv6_link_local(ip: std::net::Ipv6Addr) -> bool {
    (ip.segments()[0] & 0xffc0) == 0xfe80
}

/// The server token from `Authorization: Bearer <token>` (or the `x-server-token` header).
fn bearer_token(headers: &HeaderMap) -> Option<String> {
    if let Some(value) = headers
        .get("authorization")
        .and_then(|value| value.to_str().ok())
    {
        if let Some(token) = value
            .strip_prefix("Bearer ")
            .or_else(|| value.strip_prefix("bearer "))
        {
            let token = token.trim();
            if !token.is_empty() {
                return Some(token.to_string());
            }
        }
    }
    headers
        .get("x-server-token")
        .and_then(|value| value.to_str().ok())
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .map(str::to_string)
}

fn valid_server_token(token: &str) -> bool {
    token.len() == 64 && token.bytes().all(|byte| byte.is_ascii_hexdigit())
}

fn require_admin_api_key(
    state: &AppState,
    headers: &HeaderMap,
) -> Result<(), (StatusCode, String)> {
    let Some(expected_key) = &state.admin_api_key else {
        return Ok(());
    };

    let provided_key = headers
        .get("x-api-key")
        .and_then(|value| value.to_str().ok())
        .map(str::trim)
        .filter(|value| !value.is_empty());

    if provided_key == Some(expected_key.as_ref()) {
        Ok(())
    } else {
        Err((
            StatusCode::UNAUTHORIZED,
            "missing or invalid admin API key".to_string(),
        ))
    }
}

#[utoipa::path(
    post,
    path = "/v1/servers/heartbeat",
    request_body = RegisterServerRequest,
    responses(
        (status = 200, description = "Refreshed server row", body = DirectoryServerRecord)
    )
)]
#[allow(dead_code)]
const fn heartbeat_server_doc() {}

#[cfg(test)]
mod url_tests {
    use super::{absolutize_download_url, request_base_url};
    use axum::http::{HeaderMap, HeaderName, HeaderValue};

    fn headers(pairs: &[(&str, &str)]) -> HeaderMap {
        let mut map = HeaderMap::new();
        for (key, value) in pairs {
            map.insert(
                key.parse::<HeaderName>().unwrap(),
                HeaderValue::from_str(value).unwrap(),
            );
        }
        map
    }

    #[test]
    fn request_base_url_uses_host_and_forwarded_proto() {
        assert_eq!(
            request_base_url(&headers(&[("host", "papa-bear.cz")])).as_deref(),
            Some("http://papa-bear.cz")
        );
        assert_eq!(
            request_base_url(&headers(&[
                ("host", "papa-bear.cz"),
                ("x-forwarded-proto", "https")
            ]))
            .as_deref(),
            Some("https://papa-bear.cz")
        );
        // A chained proxy lists protos; the leftmost (closest to the client) wins.
        assert_eq!(
            request_base_url(&headers(&[
                ("host", "h"),
                ("x-forwarded-proto", "https, http")
            ]))
            .as_deref(),
            Some("https://h")
        );
        // No Host -> can't build a base.
        assert_eq!(request_base_url(&HeaderMap::new()), None);
    }

    #[test]
    fn absolutize_download_url_only_rewrites_relative() {
        let base = Some("http://papa-bear.cz".to_string());

        let mut relative = Some("/v1/mods/x/download".to_string());
        absolutize_download_url(&mut relative, base.as_deref());
        assert_eq!(
            relative.as_deref(),
            Some("http://papa-bear.cz/v1/mods/x/download")
        );

        // A missing leading slash is still joined cleanly.
        let mut bare = Some("v1/mods/x/download".to_string());
        absolutize_download_url(&mut bare, base.as_deref());
        assert_eq!(
            bare.as_deref(),
            Some("http://papa-bear.cz/v1/mods/x/download")
        );

        // A trailing slash on the base doesn't double up.
        let mut with_slash = Some("/v1/mods/x/download".to_string());
        absolutize_download_url(&mut with_slash, Some("http://papa-bear.cz/"));
        assert_eq!(
            with_slash.as_deref(),
            Some("http://papa-bear.cz/v1/mods/x/download")
        );

        // An already-absolute URL (a CDN link) is left untouched.
        let mut absolute = Some("https://cdn.example/x.pbo".to_string());
        absolutize_download_url(&mut absolute, base.as_deref());
        assert_eq!(absolute.as_deref(), Some("https://cdn.example/x.pbo"));

        // No base -> unchanged; None stays None.
        let mut no_base = Some("/v1/mods/x/download".to_string());
        absolutize_download_url(&mut no_base, None);
        assert_eq!(no_base.as_deref(), Some("/v1/mods/x/download"));
        let mut none: Option<String> = None;
        absolutize_download_url(&mut none, base.as_deref());
        assert_eq!(none, None);
    }
}
