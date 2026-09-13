export function workbookIdentity(value) {
  try {
    const url = new URL(value);
    if (url.protocol !== 'https:' || url.hostname !== '5rmarketing-my.sharepoint.com' || url.username || url.password) return null;
    const id = [...url.searchParams].find(([key]) => key.toLowerCase() === 'sourcedoc')?.[1];
    if (!id || !/^\{?[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}\}?$/i.test(id)) return null;
    return url.origin + '/' + id.replace(/[{}]/g, '').toLowerCase();
  } catch { return null; }
}
