//! Authentication backend for axum-login (TRD Section 6).
//!
//! Single-user model: credentials come from the `[auth]` config section.

use argon2::{Argon2, PasswordHash, PasswordVerifier};
use axum_login::{AuthUser, AuthnBackend, UserId};

/// Authenticated user. Since this is single-user, the username is the ID.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct User {
    pub username: String,
    /// The password hash string as bytes, used by axum-login to validate sessions.
    pw_hash_bytes: Vec<u8>,
}

impl AuthUser for User {
    type Id = String;

    fn id(&self) -> String {
        self.username.clone()
    }

    fn session_auth_hash(&self) -> &[u8] {
        &self.pw_hash_bytes
    }
}

/// Login credentials submitted via the login form.
#[derive(Clone)]
pub struct Credentials {
    pub username: String,
    pub password: String,
}

/// Authentication backend that verifies against a single configured user.
#[derive(Clone)]
pub struct Backend {
    username: String,
    password_hash: String,
}

impl Backend {
    pub fn new(username: String, password_hash: String) -> Self {
        Self {
            username,
            password_hash,
        }
    }

    fn make_user(&self) -> User {
        User {
            username: self.username.clone(),
            pw_hash_bytes: self.password_hash.as_bytes().to_vec(),
        }
    }
}

impl AuthnBackend for Backend {
    type User = User;
    type Credentials = Credentials;
    type Error = std::convert::Infallible;

    async fn authenticate(
        &self,
        creds: Self::Credentials,
    ) -> Result<Option<Self::User>, Self::Error> {
        if creds.username != self.username {
            return Ok(None);
        }

        let parsed_hash = match PasswordHash::new(&self.password_hash) {
            Ok(h) => h,
            Err(_) => return Ok(None),
        };

        let argon2 = Argon2::default();
        if argon2
            .verify_password(creds.password.as_bytes(), &parsed_hash)
            .is_ok()
        {
            Ok(Some(self.make_user()))
        } else {
            Ok(None)
        }
    }

    async fn get_user(
        &self,
        user_id: &UserId<Self>,
    ) -> Result<Option<Self::User>, Self::Error> {
        if user_id == &self.username {
            Ok(Some(self.make_user()))
        } else {
            Ok(None)
        }
    }
}
