import React from 'react';
import classnames from 'classnames';
import { Button } from 'fxdk/ui/controls/Button/Button';
import { Input } from 'fxdk/ui/controls/Input/Input';
import { Explorer } from 'fxdk/ui/Explorer/Explorer';
import { combineVisibilityFilters, visibilityFilters } from 'fxdk/ui/Explorer/Explorer.filters';
import { assetApi } from 'shared/api.events';
import { FilesystemEntry } from 'shared/api.types';
import { assetImporterTypes } from 'shared/asset.types';
import { ImporterProps } from './Importer.types';
import { getRelativePath } from 'fxdk/ui/Explorer/Explorer.utils';
import { FsAssetImportRequest } from 'backend/project/asset/importer-contributions/fs-importer/fs-importer.types';
import { PathSelector } from 'fxdk/ui/controls/PathSelector/PathSelector';
import { inferAssetName } from './Importer.utils';
import { observer } from 'mobx-react-lite';
import { ProjectState } from 'store/ProjectState';
import { Api } from 'fxdk/browser/Api';
import s from './Importer.module.scss';

const resourceFolderSelectableFilter = (entry: FilesystemEntry) => {
  return entry.isDirectory && !entry.meta.isResource;
};
const resourceFolderVisibilityFilter = combineVisibilityFilters(
  visibilityFilters.hideAssets,
  visibilityFilters.hideFiles,
  visibilityFilters.hideDotFilesAndDirs,
);

export const FsImporter = observer(function FsImporter({ onClose }: ImporterProps) {
  const project = ProjectState.project;

  const [sourcePath, setSourcePath] = React.useState('');
  const [assetName, setAssetName] = React.useState('');
  const [assetBasePath, setAssetBasePath] = React.useState(project.path);

  const canImport = sourcePath && assetName && assetBasePath;

  const doImport = React.useCallback(() => {
    if (!canImport) {
      return;
    }

    const request: FsAssetImportRequest = {
      importerType: assetImporterTypes.fs,
      assetName,
      assetBasePath,
      data: {
        sourcePath,
      },
    };

    Api.send(assetApi.import, request);
    onClose();
  }, [canImport, sourcePath, assetName, assetBasePath, onClose]);

  const handleSourcePathSelected = React.useCallback((nextSourcePath) => {
    setSourcePath(nextSourcePath);
    const inferredAssetName = inferAssetName(nextSourcePath);
    if (inferredAssetName && !assetName) {
      setAssetName(inferredAssetName);
    }
  }, [setSourcePath, setAssetName, assetName]);

  const assetBaseRelativePath = getRelativePath(project.path, assetBasePath);
  const assetPathHintHint = assetBasePath === project.path
    ? 'Location: project root'
    : `Location: ${assetBaseRelativePath}`;

  return (
    <>
      <div className="modal-label">
        Import from:
      </div>
      <PathSelector
        notOnlyFolders
        value={sourcePath}
        onChange={handleSourcePathSelected}
        className="modal-block"
      />

      <div className="modal-block">
        <Input
          value={assetName}
          onChange={setAssetName}
          label="Asset name:"
          description="If importing asset file, changing extension will change asset type, do not change it if unsure"
        />
      </div>

      <div className="modal-label">
        {assetPathHintHint}
      </div>
      <Explorer
        className={classnames(s.explorer, 'modal-block')}
        baseEntry={project.entry}
        pathsMap={project.fs}
        selectedPath={assetBasePath}
        onSelectPath={setAssetBasePath}
        selectableFilter={resourceFolderSelectableFilter}
        visibilityFilter={resourceFolderVisibilityFilter}
      />

      <div className="modal-actions">
        <Button
          theme="primary"
          text="Import"
          onClick={doImport}
          disabled={!canImport}
        />

        <Button
          text="Close"
          onClick={onClose}
        />
      </div>
    </>
  );
});
