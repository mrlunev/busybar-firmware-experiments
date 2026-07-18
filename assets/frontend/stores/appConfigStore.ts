import { defineStore } from 'pinia';

export interface SchemaFieldOption {
  value: string;
  label: string;
}

export type AppConfigValue = string | number | boolean | null;

export interface SchemaField {
  key: string;
  type: 'text' | 'number' | 'slider' | 'select' | 'toggle' | 'textarea';
  label: string;
  description?: string;
  placeholder?: string;
  secret?: boolean;
  maxLength?: number;
  min?: number;
  max?: number;
  step?: number;
  default?: AppConfigValue;
  options?: SchemaFieldOption[];
}

export interface AppSchema {
  title?: string;
  description?: string;
  layout?: 'columns';
  component?: string;
  fields: SchemaField[];
}

export interface DiscoveredApp {
  id: string;
  name: string;
  schema: AppSchema;
  config: Record<string, AppConfigValue>;
  saving: boolean;
  saveError: string | null;
}

const APPS_ROOT = '/ext/apps';
const SKIP_APP_IDS = new Set<string>([]);

export const useAppConfigStore = defineStore('appConfigStore', () => {
  const apiStore = useApiStore();

  const apps = ref<DiscoveredApp[]>([]);
  const isLoading = ref(false);
  const error = ref<string | null>(null);

  async function readStorageFile (path: string): Promise<string | null> {
    try {
      const response = await apiStore.apiRequest<Blob>(
        `/api/storage/read?path=${encodeURIComponent(path)}`,
        { responseType: 'blob' }
      );
      return await response.text();
    } catch {
      return null;
    }
  }

  async function writeStorageFile (path: string, data: string): Promise<boolean> {
    try {
      await apiStore.apiRequest(
        `/api/storage/write?path=${encodeURIComponent(path)}`,
        { method: 'POST', body: data }
      );
      return true;
    } catch {
      return false;
    }
  }

  function buildDefaults (schema: AppSchema): Record<string, AppConfigValue> {
    const defaults: Record<string, AppConfigValue> = {};
    for (const field of schema.fields) {
      if (field.default !== undefined) {
        defaults[field.key] = field.default;
      }
    }
    return defaults;
  }

  async function discoverApps () {
    isLoading.value = true;
    error.value = null;

    try {
      const listing = await apiStore.apiRequest<{ list: Array<{ name: string; type: string }> }>(`/api/storage/list?path=${encodeURIComponent(APPS_ROOT)}`);

      const dirs = listing.list.filter(item => item.type === 'dir' && !SKIP_APP_IDS.has(item.name));

      const discovered: DiscoveredApp[] = [];

      await Promise.all(dirs.map(async dir => {
        const appId = dir.name;
        const basePath = `${APPS_ROOT}/${appId}`;

        const schemaText = await readStorageFile(`${basePath}/config.schema.json`);
        if (!schemaText) {
          return;
        }

        let schema: AppSchema;
        try {
          schema = JSON.parse(schemaText);
        } catch {
          return;
        }
        if (!schema.fields || !Array.isArray(schema.fields)) {
          return;
        }

        let name = appId;
        const appJsonText = await readStorageFile(`${basePath}/app.json`);
        if (appJsonText) {
          try {
            const appJson = JSON.parse(appJsonText);
            if (appJson.name) {
              name = appJson.name;
            }
          } catch {
            // Use the folder name.
          }
        }

        const defaults = buildDefaults(schema);
        let config: Record<string, AppConfigValue> = { ...defaults };

        const configText = await readStorageFile(`${basePath}/config.json`);
        if (configText) {
          try {
            const parsed = JSON.parse(configText);
            config = { ...defaults, ...parsed };
          } catch {
            // Use schema defaults.
          }
        }

        discovered.push({
          id: appId,
          name: schema.title || name,
          schema,
          config,
          saving: false,
          saveError: null
        });
      }));

      discovered.sort((a, b) => a.name.localeCompare(b.name));
      apps.value = discovered;
    } catch (e) {
      error.value = e instanceof Error ? e.message : 'Failed to discover apps';
    } finally {
      isLoading.value = false;
    }
  }

  async function saveAppConfig (appId: string): Promise<boolean> {
    const app = apps.value.find(a => a.id === appId);
    if (!app) {
      return false;
    }

    app.saving = true;
    app.saveError = null;
    try {
      const path = `${APPS_ROOT}/${appId}/config.json`;
      const saved = await writeStorageFile(path, JSON.stringify(app.config));
      if (!saved) {
        app.saveError = 'Failed to save settings';
        return false;
      }
      return true;
    } catch (e) {
      app.saveError = e instanceof Error ? e.message : 'Failed to save settings';
      return false;
    } finally {
      app.saving = false;
    }
  }

  function updateField (appId: string, key: string, value: AppConfigValue) {
    const app = apps.value.find(a => a.id === appId);
    if (!app) {
      return;
    }
    app.config = { ...app.config, [key]: value };
  }

  return {
    apps,
    isLoading,
    error,
    discoverApps,
    saveAppConfig,
    updateField
  };
});
