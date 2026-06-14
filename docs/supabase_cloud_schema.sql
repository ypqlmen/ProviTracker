-- Provi Tracker cloud schema for Supabase.
-- The desktop app uses RPC/Edge Functions only; anon/authenticated roles have no direct table access.

create extension if not exists pgcrypto;

create table if not exists public.provi_users (
    id uuid primary key default gen_random_uuid(),
    username text not null,
    username_key text not null unique,
    password_hash text not null,
    created_at timestamptz not null default now(),
    last_login_at timestamptz
);

create table if not exists public.provi_user_data (
    user_id uuid primary key references public.provi_users(id) on delete cascade,
    settings jsonb not null default '{}'::jsonb,
    orders jsonb not null default '[]'::jsonb,
    products jsonb not null default '[]'::jsonb,
    salesperson jsonb not null default '{}'::jsonb,
    secrets jsonb not null default '{}'::jsonb,
    migrated_at timestamptz,
    updated_at timestamptz not null default now()
);

create table if not exists public.provi_sessions (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null references public.provi_users(id) on delete cascade,
    token_hash text not null unique,
    created_at timestamptz not null default now(),
    expires_at timestamptz not null default now() + interval '30 days'
);

create table if not exists public.provi_sales_registration_queue (
    id uuid primary key default gen_random_uuid(),
    user_id uuid not null references public.provi_users(id) on delete cascade,
    recipient text not null,
    payload jsonb not null,
    status text not null default 'queued',
    attempts integer not null default 0,
    created_at timestamptz not null default now(),
    sent_at timestamptz,
    last_error text
);

create index if not exists provi_sessions_user_id_idx on public.provi_sessions(user_id);
create index if not exists provi_sessions_expires_at_idx on public.provi_sessions(expires_at);
create index if not exists provi_sales_registration_queue_user_id_idx on public.provi_sales_registration_queue(user_id);
create index if not exists provi_sales_registration_queue_status_idx on public.provi_sales_registration_queue(status, created_at);

alter table public.provi_users enable row level security;
alter table public.provi_user_data enable row level security;
alter table public.provi_sessions enable row level security;
alter table public.provi_sales_registration_queue enable row level security;

revoke all on table public.provi_users from anon, authenticated;
revoke all on table public.provi_user_data from anon, authenticated;
revoke all on table public.provi_sessions from anon, authenticated;
revoke all on table public.provi_sales_registration_queue from anon, authenticated;

create or replace function public.provi_normalize_username(p_username text)
returns text
language sql
immutable
set search_path = public
as $$
    select lower(trim(coalesce(p_username, '')));
$$;

create or replace function public.provi_validate_username(p_username text)
returns boolean
language sql
immutable
set search_path = public
as $$
    select length(trim(coalesce(p_username, ''))) between 2 and 40
       and trim(coalesce(p_username, '')) ~ '^[A-Za-z0-9_.\-æøåÆØÅ]+$';
$$;

create or replace function public.provi_payload_for_user(p_user_id uuid)
returns jsonb
language sql
stable
security definer
set search_path = public
as $$
    select jsonb_build_object(
        'settings', coalesce(d.settings, '{}'::jsonb),
        'orders', coalesce(d.orders, '[]'::jsonb),
        'products', coalesce(d.products, '[]'::jsonb),
        'salesperson', coalesce(d.salesperson, '{}'::jsonb),
        'secrets', coalesce(d.secrets, '{}'::jsonb),
        'updated_at', d.updated_at,
        'migrated_at', d.migrated_at
    )
    from public.provi_users u
    left join public.provi_user_data d on d.user_id = u.id
    where u.id = p_user_id;
$$;

create or replace function public.provi_payload_has_data(p_user_id uuid)
returns boolean
language sql
stable
security definer
set search_path = public
as $$
    select coalesce(
        d.settings <> '{}'::jsonb
        or d.orders <> '[]'::jsonb
        or d.products <> '[]'::jsonb
        or d.salesperson <> '{}'::jsonb
        or d.secrets <> '{}'::jsonb,
        false
    )
    from public.provi_users u
    left join public.provi_user_data d on d.user_id = u.id
    where u.id = p_user_id;
$$;

create or replace function public.provi_create_session(p_user_id uuid)
returns jsonb
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
    v_token text := encode(extensions.gen_random_bytes(32), 'hex');
    v_hash text := encode(extensions.digest(v_token, 'sha256'), 'hex');
    v_expires timestamptz := now() + interval '30 days';
begin
    delete from public.provi_sessions where expires_at <= now();
    insert into public.provi_sessions(user_id, token_hash, expires_at)
    values (p_user_id, v_hash, v_expires);
    return jsonb_build_object('token', v_token, 'expires_at', v_expires);
end;
$$;

create or replace function public.provi_session_user_id(p_username text, p_token text)
returns uuid
language plpgsql
stable
security definer
set search_path = public, extensions
as $$
declare
    v_user_id uuid;
begin
    if coalesce(p_token, '') = '' then
        return null;
    end if;

    select u.id into v_user_id
    from public.provi_users u
    join public.provi_sessions s on s.user_id = u.id
    where u.username_key = public.provi_normalize_username(p_username)
      and s.token_hash = encode(extensions.digest(p_token, 'sha256'), 'hex')
      and s.expires_at > now()
    limit 1;

    return v_user_id;
end;
$$;

create or replace function public.provi_register(p_username text, p_password text, p_payload jsonb default '{}'::jsonb)
returns jsonb
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
    v_username text := trim(coalesce(p_username, ''));
    v_username_key text := public.provi_normalize_username(p_username);
    v_user_id uuid;
    v_session jsonb;
    v_payload jsonb := coalesce(p_payload, '{}'::jsonb);
begin
    if not public.provi_validate_username(v_username) then
        return jsonb_build_object('ok', false, 'error', 'Brugernavnet maa kun indeholde bogstaver, tal, punktum, bindestreg og underscore.');
    end if;

    if length(coalesce(p_password, '')) < 8 then
        return jsonb_build_object('ok', false, 'error', 'Adgangskoden skal vaere mindst 8 tegn.');
    end if;

    insert into public.provi_users(username, username_key, password_hash)
    values (v_username, v_username_key, extensions.crypt(p_password, extensions.gen_salt('bf')))
    returning id into v_user_id;

    insert into public.provi_user_data(user_id, settings, orders, products, salesperson, secrets, migrated_at)
    values (
        v_user_id,
        coalesce(v_payload->'settings', '{}'::jsonb),
        coalesce(v_payload->'orders', '[]'::jsonb),
        coalesce(v_payload->'products', '[]'::jsonb),
        coalesce(v_payload->'salesperson', '{}'::jsonb),
        coalesce(v_payload->'secrets', '{}'::jsonb),
        case when v_payload <> '{}'::jsonb then now() else null end
    );

    v_session := public.provi_create_session(v_user_id);

    return jsonb_build_object(
        'ok', true,
        'username', v_username,
        'token', v_session->>'token',
        'expires_at', v_session->>'expires_at',
        'has_data', public.provi_payload_has_data(v_user_id),
        'data', public.provi_payload_for_user(v_user_id)
    );
exception when unique_violation then
    return jsonb_build_object('ok', false, 'error', 'Brugernavnet findes allerede.');
end;
$$;

create or replace function public.provi_login(p_username text, p_password text)
returns jsonb
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
    v_user record;
    v_session jsonb;
begin
    select * into v_user
    from public.provi_users
    where username_key = public.provi_normalize_username(p_username)
    limit 1;

    if v_user.id is null or v_user.password_hash <> extensions.crypt(coalesce(p_password, ''), v_user.password_hash) then
        return jsonb_build_object('ok', false, 'error', 'Forkert brugernavn eller adgangskode.');
    end if;

    update public.provi_users set last_login_at = now() where id = v_user.id;
    v_session := public.provi_create_session(v_user.id);

    return jsonb_build_object(
        'ok', true,
        'username', v_user.username,
        'token', v_session->>'token',
        'expires_at', v_session->>'expires_at',
        'has_data', public.provi_payload_has_data(v_user.id),
        'data', public.provi_payload_for_user(v_user.id)
    );
end;
$$;

create or replace function public.provi_load(p_username text, p_token text)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
    v_user_id uuid := public.provi_session_user_id(p_username, p_token);
begin
    if v_user_id is null then
        return jsonb_build_object('ok', false, 'error', 'Login-sessionen er udloebet. Log ind igen.');
    end if;

    return jsonb_build_object(
        'ok', true,
        'has_data', public.provi_payload_has_data(v_user_id),
        'data', public.provi_payload_for_user(v_user_id)
    );
end;
$$;

create or replace function public.provi_save(p_username text, p_token text, p_payload jsonb)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
    v_user_id uuid := public.provi_session_user_id(p_username, p_token);
    v_payload jsonb := coalesce(p_payload, '{}'::jsonb);
begin
    if v_user_id is null then
        return jsonb_build_object('ok', false, 'error', 'Login-sessionen er udloebet. Log ind igen.');
    end if;

    insert into public.provi_user_data(user_id, settings, orders, products, salesperson, secrets, migrated_at, updated_at)
    values (
        v_user_id,
        coalesce(v_payload->'settings', '{}'::jsonb),
        coalesce(v_payload->'orders', '[]'::jsonb),
        coalesce(v_payload->'products', '[]'::jsonb),
        coalesce(v_payload->'salesperson', '{}'::jsonb),
        coalesce(v_payload->'secrets', '{}'::jsonb),
        now(),
        now()
    )
    on conflict (user_id) do update set
        settings = excluded.settings,
        orders = excluded.orders,
        products = excluded.products,
        salesperson = excluded.salesperson,
        secrets = excluded.secrets,
        migrated_at = coalesce(public.provi_user_data.migrated_at, excluded.migrated_at),
        updated_at = now();

    return jsonb_build_object('ok', true, 'data', public.provi_payload_for_user(v_user_id));
end;
$$;

create or replace function public.provi_logout(p_username text, p_token text)
returns jsonb
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
    v_user_id uuid := public.provi_session_user_id(p_username, p_token);
begin
    if v_user_id is not null then
        delete from public.provi_sessions
        where user_id = v_user_id
          and token_hash = encode(extensions.digest(p_token, 'sha256'), 'hex');
    end if;
    return jsonb_build_object('ok', true);
end;
$$;

create or replace function public.provi_enqueue_sales_registration(p_username text, p_token text, p_payload jsonb)
returns jsonb
language plpgsql
security definer
set search_path = public, extensions
as $$
declare
    v_user_id uuid := public.provi_session_user_id(p_username, p_token);
    v_payload jsonb := coalesce(p_payload, '{}'::jsonb);
    v_recipient text := trim(coalesce(v_payload->>'recipient', ''));
    v_id uuid;
begin
    if v_user_id is null then
        return jsonb_build_object('ok', false, 'error', 'Login-sessionen er udloebet. Log ind igen.');
    end if;

    if v_recipient = '' or v_recipient !~* '^[A-Z0-9._%+\-]+@[A-Z0-9.\-]+\.[A-Z]{2,}$' then
        return jsonb_build_object('ok', false, 'error', 'Salgsregistrering mangler en gyldig flow-mail.');
    end if;

    insert into public.provi_sales_registration_queue(user_id, recipient, payload)
    values (v_user_id, v_recipient, v_payload)
    returning id into v_id;

    return jsonb_build_object('ok', true, 'queue_id', v_id);
end;
$$;

create or replace function public.provi_mark_sales_registration_status(p_queue_id uuid, p_status text, p_error text default null)
returns jsonb
language plpgsql
security definer
set search_path = public
as $$
declare
    v_status text := lower(trim(coalesce(p_status, 'queued')));
begin
    if v_status not in ('queued', 'sent', 'failed') then
        return jsonb_build_object('ok', false, 'error', 'Ugyldig salgsreg-status.');
    end if;

    update public.provi_sales_registration_queue
    set status = v_status,
        attempts = attempts + case when v_status in ('sent', 'failed') then 1 else 0 end,
        sent_at = case when v_status = 'sent' then now() else sent_at end,
        last_error = p_error
    where id = p_queue_id;

    return jsonb_build_object('ok', found);
end;
$$;

grant execute on function public.provi_register(text, text, jsonb) to anon, authenticated;
grant execute on function public.provi_login(text, text) to anon, authenticated;
grant execute on function public.provi_load(text, text) to anon, authenticated;
grant execute on function public.provi_save(text, text, jsonb) to anon, authenticated;
grant execute on function public.provi_logout(text, text) to anon, authenticated;
revoke execute on function public.provi_create_session(uuid) from public, anon, authenticated;
revoke execute on function public.provi_payload_for_user(uuid) from public, anon, authenticated;
revoke execute on function public.provi_payload_has_data(uuid) from public, anon, authenticated;
revoke execute on function public.provi_session_user_id(text, text) from public, anon, authenticated;
revoke execute on function public.provi_enqueue_sales_registration(text, text, jsonb) from public, anon, authenticated;
grant execute on function public.provi_enqueue_sales_registration(text, text, jsonb) to service_role;
revoke execute on function public.provi_mark_sales_registration_status(uuid, text, text) from public, anon, authenticated;
grant execute on function public.provi_mark_sales_registration_status(uuid, text, text) to service_role;
