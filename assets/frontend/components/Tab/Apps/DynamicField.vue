<template>
  <UFormField :label="field.label" :description="field.description">
    <template v-if="field.type === 'text'">
      <UInput
        :model-value="String(modelValue ?? '')"
        :name="field.key"
        size="xl"
        variant="soft"
        :ui="{ base: 'ring-1 ring-glass bg-accented/50' }"
        :type="field.secret && !showSecret ? 'password' : 'text'"
        :placeholder="field.placeholder"
        :maxlength="field.maxLength"
        @update:model-value="$emit('update:modelValue', $event)"
      >
        <template v-if="field.secret" #trailing>
          <UButton
            :icon="showSecret ? 'i-bi-eye' : 'i-bi-eye-shut'"
            variant="ghost"
            color="neutral"
            square
            class="rounded-full"
            :ui="{ leadingIcon: 'size-6 text-muted' }"
            @click="showSecret = !showSecret"
          />
        </template>
      </UInput>
    </template>

    <template v-else-if="field.type === 'number'">
      <UInput
        :model-value="modelValue ?? field.default ?? 0"
        :name="field.key"
        type="number"
        size="xl"
        variant="soft"
        :ui="{ base: 'ring-1 ring-glass bg-accented/50' }"
        :placeholder="field.placeholder"
        :min="field.min"
        :max="field.max"
        :step="field.step"
        @update:model-value="$emit('update:modelValue', Number($event))"
      />
    </template>

    <template v-else-if="field.type === 'select'">
      <USelect
        :model-value="modelValue ?? field.default"
        :name="field.key"
        :items="selectItems"
        size="xl"
        variant="soft"
        :ui="{ base: 'ring-1 ring-glass bg-accented/50' }"
        class="w-full"
        @update:model-value="$emit('update:modelValue', $event)"
      />
    </template>

    <template v-else-if="field.type === 'toggle'">
      <USwitch
        :model-value="Boolean(modelValue ?? field.default ?? false)"
        @update:model-value="$emit('update:modelValue', $event)"
      />
    </template>

    <template v-else-if="field.type === 'slider'">
      <div class="flex flex-col gap-2.5 w-full">
        <div class="flex justify-between items-center">
          <div class="text-sm text-muted">{{ sliderDisplayValue }}{{ field.max === 100 ? '%' : '' }}</div>
        </div>
        <USlider
          :model-value="Number(modelValue ?? field.default ?? field.min ?? 0)"
          :min="field.min ?? 0"
          :max="field.max ?? 100"
          :step="field.step ?? 1"
          :ui="{
            root: '',
            track: 'h-[14px] bg-accented/50 dark:bg-accented',
            range: 'bg-primary-500 rounded-r-none',
            thumb: 'bg-primary-500 ring-4 ring-white size-[6px] focus-visible:outline-none'
          }"
          @update:model-value="$emit('update:modelValue', Number($event))"
        />
      </div>
    </template>

    <template v-else-if="field.type === 'textarea'">
      <UTextarea
        :model-value="String(modelValue ?? '')"
        :name="field.key"
        size="xl"
        variant="soft"
        :ui="{ base: 'ring-1 ring-glass bg-accented/50' }"
        :placeholder="field.placeholder"
        :maxlength="field.maxLength"
        :rows="3"
        @update:model-value="$emit('update:modelValue', $event)"
      />
    </template>
  </UFormField>
</template>

<script setup lang="ts">
import type { AppConfigValue, SchemaField } from '~/stores/appConfigStore';

const props = defineProps<{
  field: SchemaField;
  modelValue: AppConfigValue | undefined;
}>();

defineEmits<{
  'update:modelValue': [value: AppConfigValue];
}>();

const showSecret = ref(false);

const sliderDisplayValue = computed(() => {
  return Number(props.modelValue ?? props.field.default ?? props.field.min ?? 0);
});

const selectItems = computed(() => {
  if (!props.field.options) {
    return [];
  }
  return props.field.options.map(opt => ({
    value: opt.value,
    label: opt.label
  }));
});
</script>
